/*
 * Residenter Bootloader der KRONE-REW-Modulsteuerung (ATtiny1616).
 *
 * Am Geraet verifiziert: Werksflash und Sprung in die App (10.09.2026),
 * Bus-Update Ende-zu-Ende (21.09.2026, seit Firmware v1.9). Aenderungen ab
 * BL_VERSION 2 (board.h) sind nur auf dem Host getestet. Siehe
 * docs/module-bootloader.md. Erstflash + Fuse weiterhin ueber UPDI/J6.
 *
 * Lage:   Boot-Bereich 0x0000..0x0BFF (Fuse BOOTEND = 0x0C), App ab 0x0C00.
 * Ablauf: Entscheidungen trifft lib/fwupdate/fwboot (host-getestet), hier nur
 *         Hardware: USART0/RS-485 gepollt, TCB0 als 1-ms-Zeitbasis, NVMCTRL,
 *         EEPROM. Kurz:
 *         - App startbar (erstes Wort beschrieben, EEPROM[EE_APP_VALID] 0xA5
 *           oder 0xFF) und kein Marker GPIOR0 == 0xB7 -> Startfenster
 *           FWBOOT_WINDOW_MS; FW_BEGIN an die eigene Adresse startet ein
 *           Update, jeder andere Rahmen an sie oder Fensterende startet die App.
 *         - Sonst warten. Nach FW_BEGIN ist die App bis zum erfolgreichen
 *           FW_END nicht startbar; ein Abbruch fuehrt nie in die App.
 *
 * Motorsicherheit: der Bootloader konfiguriert PA7 (Triac-Treiber) NIE als
 * Ausgang -> der Transistor bleibt gesperrt, der Motor kann hier nicht bestromt
 * werden. Eigener Watchdog als Haenge-Schutz.
 */
#include "board.h"

#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "protocol.h"
#include "fwupdate.h"
#include "fwboot.h"

/* --- Hardware --------------------------------------------------------- */

static void hw_init(void)
{
    _PROTECTED_WRITE(CLKCTRL_MCLKCTRLB, 0);                /* 20 MHz, kein Vorteiler */
    _PROTECTED_WRITE(WDT_CTRLA, WDT_PERIOD_SETTING);

    /* Triac-Treiber: nur das Ausgangslatch auf low. Nach Reset ist PA7 Eingang
     * und bleibt es; wird der Bootloader ohne Reset erreicht und PA7 ist noch
     * Ausgang der App, sperrt das den Transistor, statt ihn offen zu lassen. */
    PORTA.OUTCLR = PIN_TRIAC;

    /* XDIR UND TXD muessen als Ausgang stehen, sonst kann die USART0 den
     * DI-Pin zu U2 nicht treiben (firmware/CHANGELOG.md 1.8/1.9). */
    PORTB.DIRSET = PIN_USART_XDIR | PIN_USART_TXD;
    USART0.BAUD = USART_BAUD_REG;
    USART0.CTRLA = USART_RS485_EXT_gc;                      /* gepollt, kein RXCIE */
    USART0.CTRLC = USART_CHSIZE_8BIT_gc | USART_PMODE_DISABLED_gc | USART_SBMODE_1BIT_gc;
    USART0.CTRLB = USART_RXEN_bm | USART_TXEN_bm | USART_RXMODE_NORMAL_gc;

    /* 1-ms-Takt wie in der App (TCB0 periodisch), Flag wird gepollt. */
    TCB0.CCMP = TICK_CMP;
    TCB0.CTRLB = TCB_CNTMODE_INT_gc;
    TCB0.CTRLA = TCB_ENABLE_bm;
}

static inline void wdt_pet(void) { __asm__ __volatile__("wdr"); }

/* Naechster CRC-gepruefter Rahmen in ps->frame, oder false, wenn *left_ms
 * (Restzeit, wird heruntergezaehlt) abgelaufen ist. */
static bool recv_frame(proto_parser_t *ps, uint16_t *left_ms)
{
    for (;;) {
        wdt_pet();
        if (TCB0.INTFLAGS & TCB_CAPT_bm) {
            TCB0.INTFLAGS = TCB_CAPT_bm;
            if (*left_ms <= 1u) {
                return false;
            }
            --*left_ms;
        }
        if (USART0.STATUS & USART_RXCIF_bm) {
            if (proto_parser_feed(ps, USART0.RXDATAL) == PARSE_FRAME_OK) {
                return true;
            }
        }
    }
}

static void tx(const uint8_t *p, uint8_t n)
{
    _delay_us(RESPONSE_DELAY_US);                           /* Spez. 5.6 */
    for (uint8_t i = 0; i < n; ++i) {
        while (!(USART0.STATUS & USART_DREIF_bm)) { wdt_pet(); }
        USART0.TXDATAL = p[i];
    }
    while (!(USART0.STATUS & USART_TXCIF_bm)) { wdt_pet(); }
    USART0.STATUS = USART_TXCIF_bm;
    _delay_us(200);
    for (uint16_t g = 0; g < ECHO_DRAIN_LOOPS; ++g) {      /* eigenes Echo raeumen */
        wdt_pet();
        if (USART0.STATUS & USART_RXCIF_bm) { (void)USART0.RXDATAL; }
    }
}

static void send_reply(const fwboot_t *bt)
{
    proto_frame_t f;
    f.cmd = bt->reply_cmd;
    f.addr = bt->addr;
    f.payload_len = bt->reply_len;
    for (uint8_t i = 0; i < bt->reply_len; ++i) { f.payload[i] = bt->reply[i]; }
    uint8_t buf[PROTO_MAX_FRAME];
    size_t n = proto_encode(&f, buf, sizeof(buf));
    if (n) { tx(buf, (uint8_t)n); }
}

/* Flash schreiben (Erase+Write je Seite). */
static bool write_page(void *ctx, uint32_t rel_addr, const uint8_t *page)
{
    (void)ctx;
    volatile uint16_t *dst = (volatile uint16_t *)(APP_DATA_ADDR + rel_addr);

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

static void set_marker(void *ctx, uint8_t marker)
{
    (void)ctx;
    eeprom_update_byte((uint8_t *)EE_APP_VALID, marker);
}

/* --- App-Sprung ----------------------------------------------------- */

static void start_app(void)
{
    USART0.CTRLB = 0;
    USART0.CTRLA = 0;
    PORTB.DIRCLR = PIN_USART_XDIR;
    TCB0.CTRLA = 0;                                        /* App richtet TCB0 neu ein */
    TCB0.CNT = 0;
    TCB0.INTFLAGS = TCB_CAPT_bm;
    _PROTECTED_WRITE(CPUINT_CTRLA, 0);                     /* IVSEL = 0 */
    ((void (*)(void))(BOOT_APP_BASE / 2u))();              /* Wortadresse */
    for (;;) { }                                           /* unerreichbar */
}

int main(void)
{
    cli();                                                 /* Bootloader ohne Interrupts */
    const bool forced = (GPIOR0 == STAY_MARKER);
    GPIOR0 = 0;

    hw_init();

    fwboot_t bt;
    fwboot_init(&bt, eeprom_read_byte((const uint8_t *)EE_BUS_ADDR), forced,
                fwboot_app_valid(*(const uint16_t *)APP_DATA_ADDR,
                                 eeprom_read_byte((const uint8_t *)EE_APP_VALID)),
                APP_MAX, BL_VERSION, write_page, set_marker, NULL);

    proto_parser_t ps;
    proto_parser_reset(&ps);

    for (;;) {
        /* Zeitablauf laeuft ab der letzten angenommenen Aktion; Rahmen an
         * andere Adressen oder Broadcasts setzen ihn nicht zurueck. */
        uint16_t left_ms = fwboot_timeout_ms(&bt);
        fwboot_action_t act;
        do {
            if (!recv_frame(&ps, &left_ms)) {
                act = fwboot_on_timeout(&bt);
                break;
            }
            act = fwboot_on_frame(&bt, ps.frame.cmd, ps.frame.addr,
                                  ps.frame.payload, ps.frame.payload_len);
        } while (act == FWBOOT_IGNORE);

        switch (act) {
        case FWBOOT_REPLY:
            send_reply(&bt);
            break;
        case FWBOOT_REPLY_RESET:
            send_reply(&bt);
            _delay_ms(20);
            _PROTECTED_WRITE(RSTCTRL_SWRR, RSTCTRL_SWRE_bm);
            break;
        case FWBOOT_START_APP:
            start_app();
            break;
        default:
            break;
        }
    }
}
