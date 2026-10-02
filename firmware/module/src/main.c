/*
 * Modul-Firmware der KRONE-REW-Fallblattanzeige (ATtiny1616, bare metal).
 *
 * Verdrahtet die getesteten hardwareunabhaengigen Automaten mit der Hardware:
 *   lib/protocol     Rahmen, CRC, Kommandotabelle          (Spez. 5)
 *   lib/enumeration  Busadress-Enumeration                 (Spez. 4.5)
 *   lib/motion       Bewegungs-Zustandsautomat             (Spez. 6)
 *   lib/config       EEPROM-Konfiguration                  (Spez. 6.3)
 *   lib/posring      Positions-Ringpuffer                  (Spez. 6.2)
 *
 * Hardwarekonstanten ausschliesslich in board.h.
 *
 * Peripherie:
 *   TCB0         periodischer Interrupt, 1-ms-Zeitbasis
 *   PORTA        fallende Flanke an PA4 (Blatt) und PA5 (Leerbild)
 *   USART0       RS-485-Modus, XDIR steuert DE byte-genau; /RE liegt fest auf
 *                GND, daher liest die Karte ihr Sendeecho zur Kollisionserkennung
 *   WDT          ~1 s, Fail-Safe fuer den Motor (Spez. 6.4); gefuettert nur,
 *                solange die 1-ms-Zeitbasis laeuft
 *   NVMCTRL      EEPROM-Schreiben ueber eine Warteschlange, nie blockierend
 */
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/wdt.h>
#include <util/delay.h>

#include "board.h"
#include "config.h"
#include "enumeration.h"
#include "motion.h"
#include "posring.h"
#include "protocol.h"

/* --- Zeitbasis ------------------------------------------------------- */

static volatile uint32_t g_ms;

ISR(TCB0_INT_vect)
{
    TCB0.INTFLAGS = TCB_CAPT_bm;
    g_ms++;
}

static uint32_t millis_now(void)
{
    uint32_t v;
    cli();
    v = g_ms;
    sei();
    return v;
}

/* --- Impulseingaenge ---------------------------------------------- */

static volatile uint8_t  g_blatt_flag;
static volatile uint8_t  g_leer_flag;
static volatile uint32_t g_blatt_ts;
static volatile uint32_t g_leer_ts;

ISR(PORTA_PORT_vect)
{
    const uint8_t flags = PORTA.INTFLAGS;
    if (flags & PIN_PULSE_BLATT) {
        g_blatt_ts = g_ms;
        g_blatt_flag = 1;
    }
    if (flags & PIN_PULSE_LEER) {
        g_leer_ts = g_ms;
        g_leer_flag = 1;
    }
    PORTA.INTFLAGS = flags;
}

/* Impuls atomar abholen: Merker und 32-bit-Zeitstempel ohne ISR dazwischen
 * (#39). Rueckgabe 1, wenn ein Impuls anstand. */
static uint8_t take_pulse(volatile uint8_t *flag, volatile uint32_t *ts, uint32_t *out)
{
    uint8_t got;
    cli();
    got = *flag;
    if (got) {
        *out = *ts;
        *flag = 0;
    }
    sei();
    return got;
}

/* --- USART0: Empfang und Sendeecho ------------------------------- */

#define RX_RING_LEN 64u

static volatile uint8_t rx_ring[RX_RING_LEN];
static volatile uint8_t rx_head, rx_tail;
static volatile uint8_t rx_stamp;   /* untere 8 Bit von g_ms beim letzten Empfangsbyte */

/* Waehrend einer eigenen Sendung vergleicht die RXC-ISR die ersten
 * tx_echo_len empfangenen Bytes mit dem gesendeten Puffer, statt sie an den
 * Parser zu geben. Alles danach geht wieder in den Empfangsring. */
static volatile uint8_t        tx_echo_mode;
static volatile uint8_t        tx_echo_bad;
static volatile const uint8_t *tx_echo_buf;
static volatile uint8_t        tx_echo_len;
static volatile uint8_t        tx_echo_pos;

ISR(USART0_RXC_vect)
{
    const uint8_t status = USART0.RXDATAH;
    const uint8_t data = USART0.RXDATAL;
    (void)status;

    rx_stamp = (uint8_t)g_ms;

    if (tx_echo_mode && tx_echo_pos < tx_echo_len) {
        if (data != tx_echo_buf[tx_echo_pos]) {
            tx_echo_bad = 1;
        }
        tx_echo_pos++;
        return;
    }

    const uint8_t next = (uint8_t)((rx_head + 1u) % RX_RING_LEN);
    if (next != rx_tail) {
        rx_ring[rx_head] = data;
        rx_head = next;
    }
}

static int16_t rx_pop(void)
{
    if (rx_tail == rx_head) {
        return -1;
    }
    const uint8_t b = rx_ring[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1u) % RX_RING_LEN);
    return b;
}

/* --- Peripherie-Setup ------------------------------------------- */

/*
 * Lage der Interruptvektoren (#19).
 *
 * Boot-Variante: App ab 0x0C00 hinter dem Bootloader, Vektoren hinter der
 * BOOT-Section -> IVSEL = 0 (Reset-Standard, hier explizit).
 *
 * Plain-Variante: App ab 0x0000. Steht die Fuse BOOTEND != 0 (werksgeflashte
 * Karte, Plain-Upload schreibt keine Fuses), liegt diese App in der
 * BOOT-Section und die CPU sucht die Vektoren mit IVSEL = 0 hinter ihr, also
 * mitten im Code (Disassembly in #19: TCB0-Vektor 0x0C34 springt in main,
 * kein RETI, Interrupts tot, g_ms steht). IVSEL = 1 legt die Vektoren an den
 * Anfang der BOOT-Section = 0x0000, wo die Tabelle dieser App steht; dasselbe
 * macht megaTinyCore fuer eine ueber BOOT und APP verteilte Anwendung
 * (framework-arduino-megaavr-megatinycore, cores/megatinycore/main.cpp).
 * Register und Bit: iotn1616.h FUSE.BOOTEND (0x1288), CPUINT_IVSEL_bm (0x40);
 * CPUINT.CTRLA ist CCP-geschuetzt.
 */
static void vectors_init(void)
{
#ifdef HAS_BOOTLOADER
    _PROTECTED_WRITE(CPUINT.CTRLA, 0);
#else
    if (FUSE.BOOTEND != 0u) {
        _PROTECTED_WRITE(CPUINT.CTRLA, CPUINT_IVSEL_bm);
    }
#endif
}

static void clock_init(void)
{
    /* 20-MHz-Basis ohne Vorteiler. OSCCFG-Fuse waehlt 16/20 MHz. */
    _PROTECTED_WRITE(CLKCTRL.MCLKCTRLB, 0);
}

static void gpio_init(void)
{
    PORTA.DIRSET = PIN_TRIAC | PIN_CHAIN_OUT | PIN_LED;
    PORTA.DIRCLR = PIN_PULSE_BLATT | PIN_PULSE_LEER | PIN_PULSE_NULL | PIN_CHAIN_IN;
    PORTA.OUTCLR = PIN_TRIAC | PIN_CHAIN_OUT | PIN_LED;

    /* Fallende Flanke an Blatt- und Leerbildimpuls. Externe Pull-ups auf der
     * Karte, daher kein interner Pull-up. */
    PORTA.PIN4CTRL = PORT_ISC_FALLING_gc;
    PORTA.PIN5CTRL = PORT_ISC_FALLING_gc;

    /* XDIR und TXD muessen als Ausgang stehen, damit USART0 sie tatsaechlich
     * treibt -- sonst bleibt der Pin ein unbeschalteter Eingang, egal was die
     * Peripherie intern schiebt. */
    PORTB.DIRSET = PIN_USART_XDIR | PIN_USART_TXD;
}

static void tick_init(void)
{
    TCB0.CCMP = TICK_CMP;
    TCB0.INTCTRL = TCB_CAPT_bm;
    TCB0.CTRLB = TCB_CNTMODE_INT_gc;
    TCB0.CTRLA = TCB_ENABLE_bm;  /* CLK_PER, kein Vorteiler */
}

static void usart_init(void)
{
    USART0.BAUD = USART_BAUD_REG;
    /* RS485 External Drive: USART0 treibt XDIR waehrend der Sendung plus
     * Guard-Zeit selbst (Spez. 4.2, byte-genaue DE-Steuerung). */
    USART0.CTRLA = USART_RXCIE_bm | USART_RS485_EXT_gc;
    USART0.CTRLC = USART_CHSIZE_8BIT_gc | USART_PMODE_DISABLED_gc | USART_SBMODE_1BIT_gc;
    USART0.CTRLB = USART_RXEN_bm | USART_TXEN_bm | USART_RXMODE_NORMAL_gc;
}

static void wdt_init(void)
{
    _PROTECTED_WRITE(WDT.CTRLA, WDT_PERIOD_SETTING);
}

/* --- EEPROM-Schreibwarteschlange ------------------------------------ */
/*
 * Alle Schreibvorgaenge (Konfiguration, Busadresse, Position) laufen hier
 * durch. Je Schleifendurchlauf startet hoechstens ein Byte, und nur bei
 * freiem EEPROM (NVMCTRL.STATUS EEBUSY, iotn1616.h). eeprom_write_byte wartet
 * dann nicht und kehrt nach dem Start des Erase/Write-Kommandos zurueck. So
 * verlaengert kein Schreibvorgang den Antwortverzug auf dem Bus (#21).
 * Reihenfolge bleibt erhalten (FIFO); ein erneuter Auftrag fuer dieselbe
 * Adresse ersetzt nur den Wert. Lesen sieht ausstehende Werte zuerst.
 */
#define EEQ_LEN 16u

static uint8_t eeq_addr[EEQ_LEN];
static uint8_t eeq_val[EEQ_LEN];
static uint8_t eeq_head;
static uint8_t eeq_count;

static uint8_t eeq_index(uint8_t i)
{
    return (uint8_t)((eeq_head + i) % EEQ_LEN);
}

static void eeq_service(void)
{
    if (eeq_count == 0u || (NVMCTRL.STATUS & NVMCTRL_EEBUSY_bm)) {
        return;
    }
    const uint8_t a = eeq_addr[eeq_head];
    const uint8_t v = eeq_val[eeq_head];
    eeq_head = eeq_index(1u);
    eeq_count--;
    if (eeprom_read_byte((const uint8_t *)(uintptr_t)a) != v) {
        eeprom_write_byte((uint8_t *)(uintptr_t)a, v);
    }
}

static void eeq_push(uint8_t addr, uint8_t val)
{
    for (uint8_t i = 0; i < eeq_count; ++i) {
        const uint8_t k = eeq_index(i);
        if (eeq_addr[k] == addr) {
            eeq_val[k] = val;
            return;
        }
    }
    while (eeq_count >= EEQ_LEN) {
        eeq_service();   /* Notfall: voll -> warten, bis ein Platz frei wird */
    }
    const uint8_t k = eeq_index(eeq_count);
    eeq_addr[k] = addr;
    eeq_val[k] = val;
    eeq_count++;
}

/* Alles Ausstehende schreiben und das letzte Kommando abwarten (vor Reset). */
static void eeq_flush(void)
{
    while (eeq_count != 0u) {
        eeq_service();
    }
    while (NVMCTRL.STATUS & NVMCTRL_EEBUSY_bm) {
    }
}

static uint8_t ee_read(uint8_t addr)
{
    for (uint8_t i = eeq_count; i-- > 0u;) {
        const uint8_t k = eeq_index(i);
        if (eeq_addr[k] == addr) {
            return eeq_val[k];
        }
    }
    return eeprom_read_byte((const uint8_t *)(uintptr_t)addr);
}

/* Callbacks fuer lib/posring. */
static uint8_t ring_read(void *ctx, uint16_t addr)
{
    (void)ctx;
    return ee_read((uint8_t)addr);
}

static void ring_write(void *ctx, uint16_t addr, uint8_t value)
{
    (void)ctx;
    eeq_push((uint8_t)addr, value);
}

static void load_config(module_config_t *cfg)
{
    uint8_t raw[CONFIG_SIZE];
    for (uint8_t i = 0; i < CONFIG_SIZE; ++i) {
        raw[i] = ee_read((uint8_t)(EE_CONFIG_ADDR + i));
    }
    config_from_bytes(cfg, raw);
    if (!config_validate(cfg)) {
        /* Migration/Reparatur zurueckschreiben, z. B. Flags 0xFF oder 0x07 (#18) */
        config_to_bytes(cfg, raw);
        for (uint8_t i = 0; i < CONFIG_SIZE; ++i) {
            eeq_push((uint8_t)(EE_CONFIG_ADDR + i), raw[i]);
        }
    }
}

/* --- Senden -------------------------------------------------- */

/*
 * Sendet buf und prueft das Sendeecho. Weicht ein Byte ab (Kollision,
 * Spez. 4.5.3), wird die Sendung abgebrochen: es folgen nur noch die Bytes,
 * die schon im USART-Puffer standen. Rueckgabe: true bei fehlerfreiem Echo.
 */
static bool bus_send(const uint8_t *buf, uint8_t len)
{
    /* Mindest-Antwortverzug nach Rahmenende (Spez. 5.6). */
    _delay_us(RESPONSE_DELAY_US);

    tx_echo_buf = buf;
    tx_echo_len = len;
    tx_echo_pos = 0;
    tx_echo_bad = 0;
    tx_echo_mode = 1;

    uint8_t sent = 0;
    while (sent < len && !tx_echo_bad) {
        while (!(USART0.STATUS & USART_DREIF_bm)) {
        }
        USART0.TXDATAL = buf[sent];
        sent++;
    }
    /* Nach einem Abbruch nur das Echo der gesendeten Bytes erwarten; was
     * danach kommt, gehoert nicht zu uns und geht an den Parser. */
    tx_echo_len = sent;

    while (!(USART0.STATUS & USART_TXCIF_bm)) {
    }
    USART0.STATUS = USART_TXCIF_bm;

    for (uint8_t i = 0; i < ECHO_TAIL_STEPS && tx_echo_pos < sent; ++i) {
        _delay_us(ECHO_TAIL_STEP_US);
    }
    tx_echo_mode = 0;
    return tx_echo_bad == 0u;
}

static uint8_t g_txbuf[PROTO_MAX_FRAME];

static bool send_frame(uint8_t cmd, uint8_t addr, const uint8_t *payload, uint8_t len)
{
    proto_frame_t f;
    f.cmd = cmd;
    f.addr = addr;
    f.payload_len = len;
    for (uint8_t i = 0; i < len; ++i) {
        f.payload[i] = payload[i];
    }
    const size_t n = proto_encode(&f, g_txbuf, sizeof(g_txbuf));
    if (n == 0) {
        return true;
    }
    return bus_send(g_txbuf, (uint8_t)n);
}

/* --- Anwendungszustand ------------------------------------- */

static module_config_t g_cfg;
static enum_fsm_t      g_enum;
static motion_t        g_motion;
static posring_t       g_ring;
static motion_state_t  g_prev_state;
static uint8_t         g_identify_active;
static uint32_t        g_identify_until_ms;
static uint32_t        g_led_sync_ms;   /* now-Bezugspunkt fuer synchrones Blinken */

/* Antwort senden; eine Echo-Abweichung verwirft die Laufzeitadresse (4.5.3). */
static void reply(uint8_t cmd, uint8_t own_addr, const uint8_t *payload, uint8_t len)
{
    if (!send_frame(cmd, own_addr, payload, len)) {
        enum_fsm_on_echo_mismatch(&g_enum);
    }
}

static void ack(uint8_t cmd, uint8_t own_addr)
{
    reply(cmd, own_addr, NULL, 0);
}

/* Positionsspeicher an Konfiguration und Zustand angleichen (Start,
 * SET_CONFIG): ungueltig, solange er nicht genutzt werden darf oder die Karte
 * nicht synchron ist; im synchronen Stillstand die aktuelle Lage (#24). */
static void position_sync(void)
{
    if (!config_position_store_active(&g_cfg) || !g_motion.synced) {
        (void)posring_invalidate(&g_ring);
    } else if (g_motion.state == MOTION_IDLE && g_motion.current >= 1) {
        posring_store(&g_ring, g_motion.current);   /* unveraendert: kein Schreiben */
    }
}

static void handle_frame(const proto_frame_t *f, uint32_t now)
{
    if (!proto_cmd_is_valid(f->cmd, f->addr, f->payload_len)) {
        return;
    }

    const uint8_t chain_in = pin_read(&PORTA, PIN_CHAIN_IN);
    enum_fsm_on_frame(&g_enum, f->cmd, f->addr, f->payload, f->payload_len, chain_in);

    if (g_enum.want_ack) {
        /* Erst bestaetigen; Adresse ins EEPROM und CHAIN_OUT nur nach
         * fehlerfreiem Echo (Spez. 4.5.1 Schritt 3, #20). */
        const bool ok = send_frame(CMD_ENUM_ASSIGN, g_enum.address, NULL, 0);
        enum_fsm_on_ack_sent(&g_enum, ok);
    }
    if (g_enum.want_eeprom_write) {
        g_cfg.bus_address = g_enum.eeprom_address;
        eeq_push((uint8_t)(EE_CONFIG_ADDR + CONFIG_BYTE_BUS_ADDRESS), g_enum.eeprom_address);
    }
    enum_fsm_clear_outputs(&g_enum);
    pin_set(&PORTA, PIN_CHAIN_OUT, g_enum.chain_out_active);

    const uint8_t own = enum_fsm_address(&g_enum);
    if (!enum_fsm_responds_to(&g_enum, f->addr)) {
        return;
    }
    const bool unicast = (f->addr != PROTO_ADDR_BROADCAST);

    switch (f->cmd) {
    case CMD_SET:
        motion_set_target(&g_motion, f->payload[0]);
        if (unicast) {
            ack(CMD_SET, own);
        }
        break;
    case CMD_SET_ALL:
        if (own >= 1 && own <= f->payload_len) {
            motion_set_target(&g_motion, f->payload[own - 1u]);
        }
        break;
    case CMD_GO:
        motion_go(&g_motion, now);
        break;
    case CMD_STOP:
        motion_stop(&g_motion, now);
        if (unicast) {
            ack(CMD_STOP, own);
        }
        break;
    case CMD_GET_STATUS: {
        uint8_t st[8];
        motion_fill_status(&g_motion, st, FW_VERSION);
        if (st[3] == MOTION_ERR_NONE) {
            /* Adresskollision einmal melden, wenn kein Motorfehler ansteht (#28). */
            st[3] = enum_fsm_take_error(&g_enum);
        }
        reply(CMD_GET_STATUS, own, st, sizeof(st));
        break;
    }
    case CMD_HOME:
        motion_home(&g_motion, now);
        if (unicast) {
            ack(CMD_HOME, own);
        }
        break;
    case CMD_SET_CONFIG: {
        module_config_t c = g_cfg;
        c.blattzahl = f->payload[0];
        c.blatt_offset = f->payload[1];
        c.abschaltvorhalt_ms = f->payload[2];
        c.flags = (uint8_t)(f->payload[3] & CONFIG_FLAGS_VALID_MASK);  /* Bit 2 reserviert, #18 */
        (void)config_validate(&c);
        /* ACK vor dem EEPROM-Schreiben (#21). */
        ack(CMD_SET_CONFIG, own);
        uint8_t raw[CONFIG_SIZE];
        config_to_bytes(&c, raw);
        /* nur die vier Nutzerparameter 0..3 (Spez. 5.4); Byte 4/5 (Busadresse,
         * T_enum) verwaltet die Enumeration. */
        for (uint8_t i = 0; i < 4u; ++i) {
            eeq_push((uint8_t)(EE_CONFIG_ADDR + i), raw[i]);
        }
        g_cfg = c;
        motion_apply_config(&g_motion, &g_cfg, now);   /* sofort wirksam, #23 */
        position_sync();
        break;
    }
    case CMD_GET_CONFIG: {
        const uint8_t cfg4[4] = { g_cfg.blattzahl, g_cfg.blatt_offset,
                                  g_cfg.abschaltvorhalt_ms, g_cfg.flags };
        reply(CMD_GET_CONFIG, own, cfg4, sizeof(cfg4));
        break;
    }
    case CMD_IDENTIFY:
        /* Status-LED fuer payload[0] Sekunden schnell blinken lassen. */
        g_identify_until_ms = now + (uint32_t)f->payload[0] * 1000u;
        g_identify_active = (f->payload[0] != 0u);
        if (unicast) {
            ack(CMD_IDENTIFY, own);
        }
        break;
    case CMD_LED_SYNC:
        /* Blinkphase auf den Empfangszeitpunkt nullen -- der Master sendet
         * das periodisch als Broadcast, damit alle Karten (und der Master
         * selbst) im gleichen Takt blinken statt jede seit dem eigenen
         * Boot-Zeitpunkt zu zaehlen. */
        g_led_sync_ms = now;
        break;
    case CMD_GET_UID: {
        uint8_t uid[10];
        for (uint8_t i = 0; i < 10; ++i) {
            uid[i] = ((const uint8_t *)&SIGROW.SERNUM0)[i];
        }
        reply(CMD_GET_UID, own, uid, sizeof(uid));
        break;
    }
    case CMD_PING: {
        const uint8_t v = FW_VERSION;
        reply(CMD_PING, own, &v, 1);
        break;
    }
    case CMD_GET_VERSION: {
        uint8_t flags = PROTO_VER_FLAG_APP_VALID;
#ifdef HAS_BOOTLOADER
        flags |= PROTO_VER_FLAG_BOOTLOADER;
#endif
        const uint8_t v[5] = { 1u, APP_VERSION_MAJOR, APP_VERSION_MINOR, flags, 0u };
        reply(CMD_GET_VERSION, own, v, sizeof(v));
        break;
    }
    case CMD_ENTER_BOOTLOADER:
        /* Marker fuer den Bootloader setzen und per Software-Reset neu starten.
         * Ohne residenten Bootloader ist das ein einfacher Neustart. Motor aus
         * und ausstehende EEPROM-Auftraege vorher abschliessen. */
        pin_low(&PORTA, PIN_TRIAC);
        eeq_flush();
        GPIOR0 = 0xB7u;
        _PROTECTED_WRITE(RSTCTRL.SWRR, RSTCTRL_SWRE_bm);
        break;
    default:
        break;
    }
}

/* Positionsspeicher bei Zustandswechseln fortschreiben (#24): Fahrt- oder
 * Homing-Beginn macht die gespeicherte Position ungueltig (Netzausfall
 * waehrend der Fahrt -> Homing beim naechsten Start); im synchronisierten
 * Stillstand wird die erreichte Position gespeichert, aber nur wenn
 * Positionsspeicherung an und Autohoming aus ist. */
static void track_position(void)
{
    const motion_state_t s = g_motion.state;
    if (s == g_prev_state) {
        return;
    }
    if (s == MOTION_MOVING || s == MOTION_HOMING) {
        (void)posring_invalidate(&g_ring);
    } else if (s == MOTION_IDLE && config_position_store_active(&g_cfg) &&
               g_motion.synced && g_motion.current >= 1) {
        posring_store(&g_ring, g_motion.current);
    }
    g_prev_state = s;
}

/* --- main --------------------------------------------------- */

int main(void)
{
    /* Reset-Ursache sichern und Flags loeschen (Schreiben von 1, wie
     * megaTinyCore init_reset_flags). */
    const uint8_t reset_flags = RSTCTRL.RSTFR;
    RSTCTRL.RSTFR = reset_flags;

    vectors_init();
    clock_init();
    gpio_init();
    tick_init();
    usart_init();

    load_config(&g_cfg);
    enum_fsm_init(&g_enum, g_cfg.bus_address, g_cfg.t_enum_s);
    posring_init(&g_ring, EE_POS_RING_ADDR, EE_POS_RING_SLOTS, ring_read, ring_write, NULL);
    motion_init(&g_motion, &g_cfg, posring_position(&g_ring), 0);
    if (reset_flags & RSTCTRL_WDRF_bm) {
        /* Watchdog-Reset: nicht selbsttaetig homen, sondern mit Motor aus in
         * ERROR 0x05 warten. Eine wiederholt haengende Firmware erzeugt so
         * keine Motorstoesse; das naechste GO quittiert (#19). */
        motion_enter_error(&g_motion, MOTION_ERR_RUNTIME, 0);
    }
    g_prev_state = g_motion.state;
    position_sync();

    wdt_init();
    sei();

    uint32_t last = millis_now();
    uint32_t wdt_fed_ms = last;
    proto_parser_t parser;
    proto_parser_reset(&parser);
    uint8_t rx_open = 0;   /* Parser hat Bytes eines unvollstaendigen Rahmens */

    for (;;) {
        const uint32_t now = millis_now();
        const uint16_t dt = (uint16_t)(now - last);
        last = now;

        /* Watchdog nur fuettern, wenn die 1-ms-Zeitbasis laeuft. Steht g_ms
         * (Interrupts tot), loest der WDT nach ~1 s aus, statt dass alle
         * Zeitueberwachungen des Motors unbemerkt stillstehen (#19). */
        if (now != wdt_fed_ms) {
            wdt_fed_ms = now;
            __asm__ __volatile__("wdr");
        }

        /* Impulse vor den Kommandos: ein Impuls von vor einem GO gehoert noch
         * zum alten Zustand. */
        uint32_t ts;
        if (take_pulse(&g_blatt_flag, &g_blatt_ts, &ts)) {
            motion_on_blatt_pulse(&g_motion, ts);
        }
        if (take_pulse(&g_leer_flag, &g_leer_ts, &ts)) {
            motion_on_leer_pulse(&g_motion, ts);
        }

        /* empfangene Bytes zum Parser */
        int16_t b;
        while ((b = rx_pop()) >= 0) {
            const proto_parse_result_t r = proto_parser_feed(&parser, (uint8_t)b);
            rx_open = (r == PARSE_NEED_MORE);
            if (r == PARSE_FRAME_OK) {
                handle_frame(&parser.frame, now);
            }
        }
        /* Inter-Byte-Timeout: ein angefangener Rahmen ohne Folgebyte wird
         * verworfen, statt den naechsten Rahmen zu verschlucken (#39). */
        if (rx_open) {
            const uint8_t stamp = rx_stamp;   /* vor der Zeit lesen: stamp <= jetzt */
            if ((uint8_t)((uint8_t)millis_now() - stamp) > RX_FRAME_GAP_MS) {
                proto_parser_reset(&parser);
                rx_open = 0;
            }
        }

        /* Zeitfortschritt */
        motion_tick(&g_motion, now);
        if (dt > 0) {
            enum_fsm_on_tick(&g_enum, dt);
        }

        /* Ausgaenge */
        pin_set(&PORTA, PIN_TRIAC, motion_triac_gate(&g_motion));
        pin_set(&PORTA, PIN_CHAIN_OUT, g_enum.chain_out_active);
        /* LED: Identify = schnelles Blinken (4 Hz), Fehler = langsames
         * Blinken (1 Hz), sonst Dauerlicht. 5 % Helligkeit durch
         * softwareseitiges Umschalten (~50-Hz-Traeger, kein Hardware-PWM-
         * Kanal auf diesem Pin belegt). Die Blinkphase (nicht der PWM-
         * Helligkeits-Traeger) laeuft relativ zu g_led_sync_ms, das per
         * CMD_LED_SYNC-Broadcast vom Master periodisch genullt wird --
         * sonst blinkt jede Karte seit ihrem eigenen Boot-Zeitpunkt phasenversetzt. */
        {
            const uint32_t PWM_PERIOD_MS = 20u;
            const uint32_t PWM_ON_MS     = 1u;   /* Deckel 5 % */
            const uint32_t pwm_phase     = now % PWM_PERIOD_MS;
            const uint32_t sync_now      = now - g_led_sync_ms;
            /* Identify nur mit Aktiv-Merker auswerten: der vorzeichenbehaftete
             * Vergleich allein waere nach 24,8 Tagen wieder wahr (#39). */
            if (g_identify_active && (int32_t)(g_identify_until_ms - now) <= 0) {
                g_identify_active = 0;
            }
            uint8_t led;
            if (g_identify_active) {
                led = (sync_now / 125) & 1u;  /* 4 Hz */
            } else if (g_motion.state == MOTION_ERROR) {
                led = (sync_now / 500) & 1u;  /* 1 Hz */
            } else {
                led = 1u;
            }
            pin_set(&PORTA, PIN_LED, led && (pwm_phase < PWM_ON_MS));
        }

        track_position();
        eeq_service();
    }
}
