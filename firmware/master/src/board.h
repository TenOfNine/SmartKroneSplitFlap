/*
 * Hardwarenahe Konstanten der Zentralsteuerung (ESP32-C3 Super Mini auf
 * hardware/master), an genau einer Stelle (CLAUDE.md). Pin-/UART-Belegung aus
 * docs/schaltplan-master.md Kapitel 6 (Netzliste).
 */
#ifndef KRONE_MASTER_BOARD_H
#define KRONE_MASTER_BOARD_H

#include <stdint.h>

/* --- RS-485 (TP8485E, U2) ----------------------------------------------- */
#define RS485_UART_NUM   1        /* UART1; UART0 ist die USB-Konsole */
#define PIN_RS485_RX     4        /* GPIO4 <- U2 RO */
#define PIN_RS485_TX     3        /* GPIO3 -> U2 DI */
#define PIN_RS485_DE     10       /* GPIO10 -> U2 DE, RTS des UART im Halbduplexmodus */
#define BUS_BAUD_RATE    115200u  /* Spezifikation 5.1 */

/* --- Enumeration und Anzeige -------------------------------------------- */
#define PIN_CHAIN        5        /* GPIO5 -> 74LVC1G17 -> CHAIN (high-aktiv) */
#define PIN_STATUS_LED   6        /* GPIO6 -> D1 */

/* Status-LED per Hardware-PWM (LEDC): ~5 % Helligkeit, unabhaengig von loop(). */
#define LED_PWM_HZ_VAL   2000u
#define LED_PWM_BITS_VAL 8u
#define LED_PWM_DUTY_VAL 13u      /* ~5 % von 255 */
#define LED_PWM_CHAN_VAL 0u

#endif /* KRONE_MASTER_BOARD_H */
