/*
 * Modul-Firmware der KRONE-REW-Fallblattanzeige (ATtiny1616, bare metal).
 *
 * Verdrahtet die getesteten hardwareunabhaengigen Automaten mit der Hardware:
 *   lib/protocol     Rahmen, CRC, Kommandotabelle          (Spez. 5)
 *   lib/enumeration  Busadress-Enumeration                 (Spez. 4.5)
 *   lib/motion       Bewegungs-Zustandsautomat             (Spez. 6)
 *   lib/config       EEPROM-Konfiguration                  (Spez. 6.3)
 *
 * Hardwarekonstanten ausschliesslich in board.h.
 *
 * Peripherie:
 *   TCB0         periodischer Interrupt, 1-ms-Zeitbasis
 *   PORTA        fallende Flanke an PA4 (Blatt) und PA5 (Leerbild)
 *   USART0       RS-485-Modus, XDIR steuert DE byte-genau; /RE liegt fest auf
 *                GND, daher liest die Karte ihr Sendeecho zur Kollisionserkennung
 *   WDT          ~1 s, Fail-Safe fuer den Motor (Spez. 6.4)
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

/* --- USART0: Empfang und Sendeecho ------------------------------- */

#define RX_RING_LEN 64u

static volatile uint8_t rx_ring[RX_RING_LEN];
static volatile uint8_t rx_head, rx_tail;

/* Waehrend einer eigenen Sendung vergleicht die RXC-ISR das Empfangene mit
 * dem gesendeten Puffer, statt es an den Parser zu geben. */
static volatile uint8_t        tx_echo_mode;
static volatile uint8_t        tx_echo_bad;
static volatile const uint8_t *tx_echo_buf;
static volatile uint16_t       tx_echo_len;
static volatile uint16_t       tx_echo_pos;

ISR(USART0_RXC_vect)
{
    const uint8_t status = USART0.RXDATAH;
    const uint8_t data = USART0.RXDATAL;
    (void)status;

    if (tx_echo_mode) {
        if (tx_echo_pos < tx_echo_len) {
            if (data != tx_echo_buf[tx_echo_pos]) {
                tx_echo_bad = 1;
            }
            tx_echo_pos++;
        }
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

    PORTB.DIRSET = PIN_USART_XDIR;  /* XDIR wird von USART0 getrieben */
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

/* --- EEPROM ---------------------------------------------------- */

static void load_config(module_config_t *cfg)
{
    uint8_t raw[CONFIG_SIZE];
    eeprom_read_block(raw, (const void *)(uintptr_t)EE_CONFIG_ADDR, CONFIG_SIZE);
    config_from_bytes(cfg, raw);
    if (!config_validate(cfg)) {
        config_to_bytes(cfg, raw);
        eeprom_update_block(raw, (void *)(uintptr_t)EE_CONFIG_ADDR, CONFIG_SIZE);
    }
}

static void save_bus_address(uint8_t addr)
{
    eeprom_update_byte((uint8_t *)(uintptr_t)(EE_CONFIG_ADDR + 4u), addr);
}

/* Positions-Ringpuffer: je Slot (seq, blatt). Neuester Slot = groesste seq
 * modulo 256. */
static uint8_t load_position(void)
{
    uint8_t best_seq = 0, best_pos = 0;
    int found = 0;
    for (uint8_t i = 0; i < EE_POS_RING_SLOTS; ++i) {
        const uint16_t base = (uint16_t)(EE_POS_RING_ADDR + (uint16_t)i * 2u);
        const uint8_t seq = eeprom_read_byte((const uint8_t *)(uintptr_t)base);
        const uint8_t pos = eeprom_read_byte((const uint8_t *)(uintptr_t)(base + 1u));
        if (seq == 0xFF) {
            continue;  /* leerer Slot */
        }
        if (!found || (uint8_t)(seq - best_seq) < 0x80u) {
            best_seq = seq;
            best_pos = pos;
            found = 1;
        }
    }
    return found ? best_pos : 0u;
}

static void store_position(uint8_t pos)
{
    /* naechsten Slot anhand der hoechsten seq waehlen */
    uint8_t best_seq = 0, best_slot = 0;
    int found = 0;
    for (uint8_t i = 0; i < EE_POS_RING_SLOTS; ++i) {
        const uint16_t base = (uint16_t)(EE_POS_RING_ADDR + (uint16_t)i * 2u);
        const uint8_t seq = eeprom_read_byte((const uint8_t *)(uintptr_t)base);
        if (seq == 0xFF) {
            continue;
        }
        if (!found || (uint8_t)(seq - best_seq) < 0x80u) {
            best_seq = seq;
            best_slot = i;
            found = 1;
        }
    }
    const uint8_t slot = found ? (uint8_t)((best_slot + 1u) % EE_POS_RING_SLOTS) : 0u;
    const uint8_t seq = found ? (uint8_t)(best_seq + 1u) : 1u;
    const uint16_t base = (uint16_t)(EE_POS_RING_ADDR + (uint16_t)slot * 2u);
    eeprom_update_byte((uint8_t *)(uintptr_t)base, seq);
    eeprom_update_byte((uint8_t *)(uintptr_t)(base + 1u), pos);
}

/* --- Senden -------------------------------------------------- */

static void bus_send(const uint8_t *buf, uint16_t len)
{
    /* Mindest-Antwortverzug nach Rahmenende (Spez. 5.6). */
    _delay_us(RESPONSE_DELAY_US);

    tx_echo_buf = buf;
    tx_echo_len = len;
    tx_echo_pos = 0;
    tx_echo_bad = 0;
    tx_echo_mode = 1;

    for (uint16_t i = 0; i < len; ++i) {
        while (!(USART0.STATUS & USART_DREIF_bm)) {
        }
        USART0.TXDATAL = buf[i];
    }
    while (!(USART0.STATUS & USART_TXCIF_bm)) {
    }
    USART0.STATUS = USART_TXCIF_bm;

    /* Echo einlaufen lassen (Guard-Zeit + letztes Byte). */
    _delay_us(200);
    tx_echo_mode = 0;
}

static uint8_t g_txbuf[PROTO_MAX_FRAME];

static void send_frame(uint8_t cmd, uint8_t addr, const uint8_t *payload, uint8_t len)
{
    proto_frame_t f;
    f.cmd = cmd;
    f.addr = addr;
    f.payload_len = len;
    for (uint8_t i = 0; i < len; ++i) {
        f.payload[i] = payload[i];
    }
    const size_t n = proto_encode(&f, g_txbuf, sizeof(g_txbuf));
    if (n > 0) {
        bus_send(g_txbuf, (uint16_t)n);
    }
}

/* --- Anwendungszustand ------------------------------------- */

static module_config_t g_cfg;
static enum_fsm_t      g_enum;
static motion_t        g_motion;
static motion_state_t  g_prev_state;
static uint32_t        g_identify_until_ms;

static void ack(uint8_t cmd, uint8_t own_addr)
{
    send_frame(cmd, own_addr, NULL, 0);
}

static void handle_frame(const proto_frame_t *f, uint32_t now)
{
    if (!proto_cmd_is_valid(f->cmd, f->addr, f->payload_len)) {
        return;
    }

    const uint8_t chain_in = pin_read(&PORTA, PIN_CHAIN_IN);
    enum_fsm_on_frame(&g_enum, f->cmd, f->addr, f->payload, f->payload_len, chain_in);

    if (g_enum.want_ack) {
        ack(CMD_ENUM_ASSIGN, g_enum.address);
    }
    if (g_enum.want_eeprom_write) {
        save_bus_address(g_enum.address);
    }
    enum_fsm_clear_outputs(&g_enum);

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
        send_frame(CMD_GET_STATUS, own, st, sizeof(st));
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
        c.flags = f->payload[3];
        config_validate(&c);
        uint8_t raw[CONFIG_SIZE];
        config_to_bytes(&c, raw);
        /* nur die vier Nutzerparameter 0..3 (Spez. 5.4); Byte 4/5 (Busadresse,
         * T_enum) verwaltet die Enumeration. */
        eeprom_update_block(raw, (void *)(uintptr_t)EE_CONFIG_ADDR, 4);
        g_cfg = c;
        ack(CMD_SET_CONFIG, own);
        break;
    }
    case CMD_GET_CONFIG: {
        const uint8_t cfg4[4] = { g_cfg.blattzahl, g_cfg.blatt_offset,
                                  g_cfg.abschaltvorhalt_ms, g_cfg.flags };
        send_frame(CMD_GET_CONFIG, own, cfg4, sizeof(cfg4));
        break;
    }
    case CMD_IDENTIFY:
        /* Status-LED fuer payload[0] Sekunden schnell blinken lassen. */
        g_identify_until_ms = now + (uint32_t)f->payload[0] * 1000u;
        if (unicast) {
            ack(CMD_IDENTIFY, own);
        }
        break;
    case CMD_GET_UID: {
        uint8_t uid[10];
        for (uint8_t i = 0; i < 10; ++i) {
            uid[i] = ((const uint8_t *)&SIGROW.SERNUM0)[i];
        }
        send_frame(CMD_GET_UID, own, uid, sizeof(uid));
        break;
    }
    case CMD_PING: {
        const uint8_t v = FW_VERSION;
        send_frame(CMD_PING, own, &v, 1);
        break;
    }
    default:
        break;
    }
}

/* --- main --------------------------------------------------- */

int main(void)
{
    clock_init();
    gpio_init();
    tick_init();
    usart_init();

    load_config(&g_cfg);
    enum_fsm_init(&g_enum, g_cfg.bus_address, g_cfg.t_enum_s);
    motion_init(&g_motion, &g_cfg, load_position(), 0);
    g_prev_state = g_motion.state;

    wdt_init();
    sei();

    uint32_t last = millis_now();
    proto_parser_t parser;
    proto_parser_reset(&parser);

    for (;;) {
        __asm__ __volatile__("wdr");

        const uint32_t now = millis_now();
        const uint16_t dt = (uint16_t)(now - last);
        last = now;

        /* empfangene Bytes zum Parser */
        int16_t b;
        while ((b = rx_pop()) >= 0) {
            if (proto_parser_feed(&parser, (uint8_t)b) == PARSE_FRAME_OK) {
                handle_frame(&parser.frame, now);
            }
        }

        /* Impulse */
        if (g_blatt_flag) {
            const uint32_t ts = g_blatt_ts;
            g_blatt_flag = 0;
            motion_on_blatt_pulse(&g_motion, ts);
        }
        if (g_leer_flag) {
            const uint32_t ts = g_leer_ts;
            g_leer_flag = 0;
            motion_on_leer_pulse(&g_motion, ts);
        }

        /* Zeitfortschritt */
        motion_tick(&g_motion, now);
        if (dt > 0) {
            enum_fsm_on_tick(&g_enum, dt);
        }

        /* Kollisionserkennung: eigenes Sendeecho weicht ab (Spez. 4.5.3) */
        if (tx_echo_bad) {
            tx_echo_bad = 0;
            enum_fsm_on_echo_mismatch(&g_enum);
        }

        /* Ausgaenge */
        pin_set(&PORTA, PIN_TRIAC, motion_triac_gate(&g_motion));
        pin_set(&PORTA, PIN_CHAIN_OUT, g_enum.chain_out_active);
        /* LED: Identify = schnelles Blinken, Fehler = langsames Blinken,
         * sonst Dauerlicht. */
        {
            uint8_t led;
            if ((int32_t)(g_identify_until_ms - now) > 0) {
                led = (now >> 6) & 1u;
            } else if (g_motion.state == MOTION_ERROR) {
                led = (now >> 8) & 1u;
            } else {
                led = 1u;
            }
            pin_set(&PORTA, PIN_LED, led);
        }

        /* Position nach jedem Stillstand sichern */
        if (g_motion.state != g_prev_state) {
            if (g_motion.state == MOTION_IDLE && g_motion.position_save &&
                g_motion.current >= 1) {
                store_position(g_motion.current);
            }
            g_prev_state = g_motion.state;
        }
    }
}
