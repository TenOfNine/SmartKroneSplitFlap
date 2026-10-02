/*
 * board.h -- die EINZIGE Datei mit hardwarenahen Konstanten des Bootloaders
 * (CLAUDE.md). Wo Bootloader und App dieselbe Hardware ansprechen, gelten die
 * Werte aus firmware/module/src/board.h; Aenderungen dort hier nachziehen.
 *
 * Zeiten des Ablaufs (Startfenster, Leerlauf) und die Marker-Werte stehen bei
 * der Entscheidungslogik (firmware/module/lib/fwupdate/fwboot.h).
 */
#ifndef KRONE_BOOTLOADER_BOARD_H
#define KRONE_BOOTLOADER_BOARD_H

#include <avr/io.h>
#include <stdint.h>

#ifndef F_CPU
#define F_CPU 20000000UL
#endif

/* --- Version ----------------------------------------------------- */
/* Byte 4 der GET_VERSION-Antwort im Bootloader. Bei jeder Aenderung am
 * Bootloader erhoehen (erreicht die Karten nur per UPDI-Werksflash).
 *   1  bis v1.15 (inkl. DI-Pin-Fix 3d65464)
 *   2  App-Gueltigkeit nach Abbruch, wiederaufsetzbare Uebertragung, TCB0-Zeitbasis */
#define BL_VERSION        2u

/* --- Flash-Aufteilung (Fuse BOOTEND = 0x0C) ---------------------- */
#ifndef BOOT_APP_BASE
#define BOOT_APP_BASE     0x0C00u
#endif
#define APP_MAX           ((uint32_t)PROGMEM_SIZE - BOOT_APP_BASE)
/* App-Bereich im Datenadressraum (Lesen + Seitenpuffer schreiben). */
#define APP_DATA_ADDR     (MAPPED_PROGMEM_START + BOOT_APP_BASE)

/* --- Marker und EEPROM ------------------------------------------- */
#define STAY_MARKER       0xB7u   /* GPIOR0, gesetzt von CMD_ENTER_BOOTLOADER der App */
#define EE_BUS_ADDR       4u      /* lib/config: bytes[4] = bus_address */
#define EE_APP_VALID      8u      /* Marker "App gueltig", Werte in fwboot.h */

/* --- Pins -------------------------------------------------------- */
/* PA7 = Triac-Treiber. Der Bootloader setzt nur das Ausgangslatch auf low,
 * PA7 nie als Ausgang. */
#define PIN_TRIAC         PIN7_bm
/* USART0 Standard-MUX: TXD = PB2, RXD = PB3, XDIR = PB0. */
#define PIN_USART_XDIR    PIN0_bm
#define PIN_USART_TXD     PIN2_bm

/* --- Takt und Bus ------------------------------------------------ */
#define BUS_BAUD          115200UL
#define USART_BAUD_REG    ((uint16_t)((4UL * F_CPU) / BUS_BAUD))
/* 1-ms-Zeitbasis: TCB0 periodisch wie in der App, hier ohne Interrupt gepollt. */
#define TICK_CMP          ((uint16_t)(F_CPU / 1000UL) - 1u)
#define RESPONSE_DELAY_US 200u    /* Mindest-Antwortverzug nach Rahmenende (Spez. 5.6) */
#define ECHO_DRAIN_LOOPS  4000u   /* eigenes Echo nach dem Senden verwerfen */

/* --- Watchdog ---------------------------------------------------- */
#define WDT_PERIOD_SETTING WDT_PERIOD_1KCLK_gc   /* ~1 s Haenge-Schutz */

#endif /* KRONE_BOOTLOADER_BOARD_H */
