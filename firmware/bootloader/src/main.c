/*
 * Residenter Bootloader der KRONE-REW-Modulsteuerung (ATtiny1616).
 *
 * EXPERIMENTELL -- am Geraet noch nicht verifiziert. Siehe docs/module-bootloader.md
 * und die Bench-Test-Checkliste im PR. Erstflash + Fuse weiterhin ueber UPDI/J6.
 *
 * Lage:   Boot-Bereich 0x0000..0x0BFF (Fuse BOOTEND = 0x0C), App ab 0x0C00.
 * Ablauf: Reset -> hier. Marker GPIOR0 == 0xB7 (von CMD_ENTER_BOOTLOADER) ODER
 *         EEPROM[EE_APP_VALID] != 0xA5  -> im Bootloader bleiben und auf
 *         CMD_FW_BEGIN warten. Sonst (gueltige App, kein Marker, kurzes
 *         Wartefenster ohne FW_BEGIN) -> Sprung in die App.
 *
 * Motorsicherheit: der Bootloader konfiguriert PA7 (Triac-Treiber) NIE als
 * Ausgang -> der Transistor bleibt gesperrt, der Motor kann hier nicht bestromt
 * werden. Eigener Watchdog als Haenge-Schutz.
 */
#include <avr/io.h>
#include <avr/eeprom.h>
#include <util/delay.h>
#include <string.h>

#include "protocol.h"
#include "fwupdate.h"

#ifndef F_CPU
#define F_CPU 20000000UL
#endif
#ifndef BOOT_APP_BASE
#define BOOT_APP_BASE 0x0C00u
#endif

#define BL_VERSION      1u
#define STAY_MARKER     0xB7u   /* GPIOR0 */
#define EE_BUS_ADDR     4u      /* config.c: bytes[4] = bus_address           */
#define EE_APP_VALID    8u      /* frei; 0xA5 = App vollstaendig geschrieben  */
#define APP_VALID_MAGIC 0xA5u

#define FLASH_DATA_BASE 0x8000u
#define APP_MAX         (0x4000u - BOOT_APP_BASE)
#define STAY_TIMEOUT_MS 2500u
#define RX_IDLE_MS      3000u

/* --- USART0 / RS-485 (gleiche Beschaltung wie die App) ------------------- */

static void usart_init(void)
{
    PORTB.DIRSET = PIN0_bm;                                 /* XDIR */
    USART0.BAUD = (uint16_t)((4UL * F_CPU) / 115200UL);
    USART0.CTRLA = USART_RS485_EXT_gc;                      /* gepollt, kein RXCIE */
    USART0.CTRLC = USART_CHSIZE_8BIT_gc | USART_PMODE_DISABLED_gc | USART_SBMODE_1BIT_gc;
    USART0.CTRLB = USART_RXEN_bm | USART_TXEN_bm | USART_RXMODE_NORMAL_gc;
}

static inline void wdt_pet(void) { __asm__ __volatile__("wdr"); }

static int16_t rx_byte(uint16_t timeout_ms)
{
    while (timeout_ms--) {
        for (uint16_t i = 0; i < 3200; ++i) {              /* ~1 ms bei 20 MHz */
            wdt_pet();
            if (USART0.STATUS & USART_RXCIF_bm) {
                return (int16_t)USART0.RXDATAL;
            }
        }
    }
    return -1;
}

static void tx(const uint8_t *p, uint8_t n)
{
    for (uint8_t i = 0; i < n; ++i) {
        while (!(USART0.STATUS & USART_DREIF_bm)) { wdt_pet(); }
        USART0.TXDATAL = p[i];
    }
    while (!(USART0.STATUS & USART_TXCIF_bm)) { wdt_pet(); }
    USART0.STATUS = USART_TXCIF_bm;
    _delay_us(200);
    for (uint16_t g = 0; g < 4000; ++g) {                  /* eigenes Echo raeumen */
        wdt_pet();
        if (USART0.STATUS & USART_RXCIF_bm) { (void)USART0.RXDATAL; }
    }
}

static void send_frame(uint8_t cmd, uint8_t addr, const uint8_t *pl, uint8_t len)
{
    proto_frame_t f;
    f.cmd = cmd;
    f.addr = addr;
    f.payload_len = len;
    for (uint8_t i = 0; i < len; ++i) { f.payload[i] = pl[i]; }
    uint8_t buf[PROTO_MAX_FRAME];
    size_t n = proto_encode(&f, buf, sizeof(buf));
    if (n) { tx(buf, (uint8_t)n); }
}

static void send_ack(uint8_t cmd, uint8_t addr, uint8_t code)
{
    send_frame(cmd, addr, &code, 1);
}

/* --- Flash schreiben (Erase+Write je Seite) ---------------------------- */

static bool write_page(void *ctx, uint32_t rel_addr, const uint8_t *page)
{
    (void)ctx;
    volatile uint16_t *dst =
        (volatile uint16_t *)(FLASH_DATA_BASE + BOOT_APP_BASE + rel_addr);

    while (NVMCTRL.STATUS & (NVMCTRL_FBUSY_bm | NVMCTRL_EEBUSY_bm)) { wdt_pet(); }
    _PROTECTED_WRITE_SPM(NVMCTRL_CTRLA, NVMCTRL_CMD_PAGEBUFCLR_gc);
    while (NVMCTRL.STATUS & NVMCTRL_FBUSY_bm) { wdt_pet(); }

    for (uint8_t i = 0; i < FWUPDATE_PAGE; i += 2) {
        dst[i / 2] = (uint16_t)(page[i] | (page[i + 1] << 8));
    }
    _PROTECTED_WRITE_SPM(NVMCTRL_CTRLA, NVMCTRL_CMD_PAGEERASEWRITE_gc);
    while (NVMCTRL.STATUS & NVMCTRL_FBUSY_bm) { wdt_pet(); }

    return (NVMCTRL.STATUS & NVMCTRL_WRERROR_bm) == 0;
}

/* Blockiert bis zum naechsten gueltigen Rahmen an uns / Broadcast / Service,
 * oder bis idle_ms Stille. true = Rahmen in *out. */
static bool recv_frame(proto_parser_t *ps, uint8_t own, proto_frame_t *out, uint16_t idle_ms)
{
    for (;;) {
        int16_t b = rx_byte(idle_ms);
        if (b < 0) { return false; }
        if (proto_parser_feed(ps, (uint8_t)b) == PARSE_FRAME_OK) {
            const uint8_t a = ps->frame.addr;
            if (a == own || a == PROTO_ADDR_BROADCAST || a == PROTO_ADDR_SERVICE) {
                *out = ps->frame;
                return true;
            }
        }
    }
}

/* --- App-Sprung ----------------------------------------------------- */

static void start_app(void)
{
    USART0.CTRLB = 0;
    USART0.CTRLA = 0;
    PORTB.DIRCLR = PIN0_bm;
    _PROTECTED_WRITE(CPUINT_CTRLA, 0);                     /* IVSEL = 0 */
    ((void (*)(void))(BOOT_APP_BASE / 2u))();              /* Wortadresse */
    for (;;) { }                                           /* unerreichbar */
}

static bool app_valid(void)
{
    /* App gilt als gueltig, wenn das erste Wort geschrieben ist UND der Marker
     * nicht auf "Update laeuft" (0x00) steht. Frisches EEPROM (0xFF) und der
     * Erfolgs-Marker (0xA5) zaehlen beide als ok. */
    if (*(const uint16_t *)(FLASH_DATA_BASE + BOOT_APP_BASE) == 0xFFFFu) {
        return false;
    }
    return eeprom_read_byte((const uint8_t *)EE_APP_VALID) != 0x00u;
}

static void reply_version(uint8_t own, bool valid)
{
    uint8_t v[5] = {
        1u, 0xFFu, 0xFFu,
        (uint8_t)(PROTO_VER_FLAG_BOOTLOADER | (valid ? PROTO_VER_FLAG_APP_VALID : 0u)),
        BL_VERSION,
    };
    send_frame(CMD_GET_VERSION, own, v, 5);
}

/* Empfaengt den Datenstrom nach einem FW_BEGIN. Kehrt zurueck (App bleibt
 * ungueltig) wenn der Transfer scheitert; startet bei Erfolg per Reset neu. */
static void do_update(proto_parser_t *ps, uint8_t own, const proto_frame_t *begin)
{
    const uint32_t total   = (uint32_t)begin->payload[0] | ((uint32_t)begin->payload[1] << 8);
    const uint16_t want_crc = (uint16_t)begin->payload[2] | ((uint16_t)begin->payload[3] << 8);

    if (total == 0u || total > APP_MAX) {
        send_ack(CMD_FW_BEGIN, own, 0x00);
        return;
    }

    eeprom_update_byte((uint8_t *)EE_APP_VALID, 0x00);

    fwupdate_t fu;
    fwupdate_begin(&fu, write_page, NULL, total, want_crc);
    send_ack(CMD_FW_BEGIN, own, 0x01);

    for (;;) {
        proto_frame_t d;
        if (!recv_frame(ps, own, &d, RX_IDLE_MS)) {
            return;   /* Abbruch -> Master wiederholt (App noch ungueltig) */
        }
        if (d.cmd == CMD_FW_DATA && d.payload_len >= 2u) {
            const uint32_t off = (uint32_t)d.payload[0] | ((uint32_t)d.payload[1] << 8);
            fwupdate_result_t rr =
                fwupdate_chunk(&fu, off, &d.payload[2], (uint8_t)(d.payload_len - 2u));
            send_ack(CMD_FW_DATA, own, rr == FWUPDATE_OK ? 0x01 : 0x00);
            if (rr != FWUPDATE_OK) { return; }
        } else if (d.cmd == CMD_FW_END) {
            fwupdate_result_t rr = fwupdate_finish(&fu);
            if (rr == FWUPDATE_OK) {
                eeprom_update_byte((uint8_t *)EE_APP_VALID, APP_VALID_MAGIC);
                send_ack(CMD_FW_END, own, 0x01);
                _delay_ms(20);
                _PROTECTED_WRITE(RSTCTRL_SWRR, RSTCTRL_SWRE_bm);
            } else {
                send_ack(CMD_FW_END, own, (uint8_t)rr);
            }
            return;
        }
        /* alles andere im Update-Modus ignorieren */
    }
}

int main(void)
{
    _PROTECTED_WRITE(CLKCTRL_MCLKCTRLB, 0);                /* 20 MHz, kein Vorteiler */

    const bool forced = (GPIOR0 == STAY_MARKER);
    GPIOR0 = 0;

    _PROTECTED_WRITE(WDT_CTRLA, WDT_PERIOD_1KCLK_gc);      /* ~1 s Haenge-Schutz */

    const bool valid = app_valid();
    usart_init();
    const uint8_t own = eeprom_read_byte((const uint8_t *)EE_BUS_ADDR);

    proto_parser_t ps;
    proto_parser_reset(&ps);

    /* Normalfall: gueltige App, kein Marker -> kurzes Fenster, dann App. */
    if (!forced && valid) {
        proto_frame_t f;
        if (recv_frame(&ps, own, &f, STAY_TIMEOUT_MS) && f.cmd == CMD_FW_BEGIN) {
            do_update(&ps, own, &f);
        }
        start_app();
    }

    /* Erzwungen oder keine gueltige App: warten, bis ein Update kommt. */
    for (;;) {
        proto_frame_t f;
        if (!recv_frame(&ps, own, &f, RX_IDLE_MS)) {
            if (valid) { start_app(); }   /* Marker gesetzt, aber App ok: doch starten */
            continue;
        }
        if (f.cmd == CMD_GET_VERSION) {
            reply_version(own, valid);
        } else if (f.cmd == CMD_FW_BEGIN) {
            do_update(&ps, own, &f);
        }
    }
}
