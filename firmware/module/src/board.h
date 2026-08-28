/*
 * board.h -- die EINZIGE Datei mit hardwarenahen Konstanten der Modul-Firmware
 * (CLAUDE.md: "Alle hardwarenahen Konstanten in genau einer Headerdatei").
 *
 * Zielbaustein: ATtiny1616, SOIC-20, 20 MHz interner Oszillator.
 * Verhaltenskonstanten (Zeiten, Fehlercodes) stehen bei ihrem Automaten
 * (lib/motion, lib/enumeration), nicht hier.
 *
 * Pinbelegung nach docs/schaltplan-daughtercard.md 6.3 und docs/pruefpunkte-t7.md.
 */
#ifndef KRONE_BOARD_H
#define KRONE_BOARD_H

#include <avr/io.h>
#include <stdint.h>

#ifndef F_CPU
#define F_CPU 20000000UL
#endif

/* --- Pins ------------------------------------------------------------- */
/* Alle Signalpins liegen an PORTA. USART0 nutzt PORTB (Standard-MUX). */

#define PIN_PULSE_BLATT  PIN4_bm   /* PA4, fallende Flanke = Zaehlimpuls */
#define PIN_PULSE_LEER   PIN5_bm   /* PA5, fallende Flanke = Leerbildimpuls */
#define PIN_PULSE_NULL   PIN6_bm   /* PA6, nur Messpunkt, Firmware wertet nicht aus */
#define PIN_TRIAC        PIN7_bm   /* PA7, Ausgang zum Transistorschalter */
#define PIN_CHAIN_IN     PIN1_bm   /* PA1, Eingang (externer Pulldown R12) */
#define PIN_CHAIN_OUT    PIN2_bm   /* PA2, Ausgang, gibt die naechste Karte frei */
#define PIN_LED          PIN3_bm   /* PA3, Status-LED (aktiv high) */

#define PORT_SIGNALS     PORTA

/* USART0 Standard-MUX: TXD = PB2, RXD = PB3, XDIR = PB0. */
#define PIN_USART_XDIR   PIN0_bm
#define PORT_USART       PORTB

/* --- Takt ----------------------------------------------------------- */
/* USART0 asynchron, Normalmodus: BAUD = (4 * F_CPU) / Baudrate. */
#define BUS_BAUD         115200UL
#define USART_BAUD_REG   ((uint16_t)((4UL * F_CPU) / BUS_BAUD))

/* 1-ms-Zeitbasis ueber TCB0 im periodischen Interruptmodus. */
#define TICK_CMP         ((uint16_t)(F_CPU / 1000UL) - 1u)

/* --- Zeitverhalten Bus (Spezifikation 5.6) ------------------------- */
#define RESPONSE_DELAY_US 200u   /* Mindest-Antwortverzug nach Rahmenende */

/* --- Watchdog (Spezifikation 6.4, Zeitbasis ~1 s) ------------------ */
#define WDT_PERIOD_SETTING WDT_PERIOD_1KCLK_gc

/* --- EEPROM-Aufteilung -------------------------------------------- */
/* Byte 0..5  Konfiguration (config.h / Spezifikation 6.3)
 * Byte 16..47  Positions-Ringpuffer: 16 Paare (seq, blatt), Verschleissschutz */
#define EE_CONFIG_ADDR    0u
#define EE_POS_RING_ADDR   16u
#define EE_POS_RING_SLOTS  16u

/* --- Firmware-Version (Statusbyte 7, PING-Antwort) ---------------- */
#define FW_VERSION 1u

/* --- kleine Pin-Helfer ------------------------------------------- */
static inline void pin_high(PORT_t *port, uint8_t bm) { port->OUTSET = bm; }
static inline void pin_low(PORT_t *port, uint8_t bm)  { port->OUTCLR = bm; }
static inline void pin_set(PORT_t *port, uint8_t bm, uint8_t on)
{
    if (on) {
        port->OUTSET = bm;
    } else {
        port->OUTCLR = bm;
    }
}
static inline uint8_t pin_read(PORT_t *port, uint8_t bm)
{
    return (port->IN & bm) ? 1u : 0u;
}

#endif /* KRONE_BOARD_H */
