/*
 * Zustandsautomat der Busadress-Enumeration fuer die Modulsteuerung.
 *
 * Hardwareunabhaengig, auf dem Host testbar. Der Automat kennt weder EEPROM
 * noch CHAIN-Leitung noch USART: Der Aufrufer speist Ereignisse ein und wertet
 * die Ausgabeflags aus.
 *
 * Grundlage: docs/spezifikation.md
 *   4.5.1  Enumeration (ENUM_RESET / ENUM_ASSIGN / ENUM_DONE, CHAIN)
 *   4.5.2  Rueckfallverhalten (T_enum, EEPROM-Adresse, Serviceadresse 250)
 *   4.5.3  Kollisionserkennung ueber das Sendeecho (Fehlercode 0x06)
 */
#ifndef KRONE_ENUMERATION_H
#define KRONE_ENUMERATION_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ENUM_UNADDRESSED = 0,  /* keine Busadresse, nicht in Enumeration */
    ENUM_ENUMERATING,      /* nach ENUM_RESET, wartet auf ENUM_ASSIGN */
    ENUM_ADDRESSED,        /* betriebsbereit unter einer Busadresse */
} enum_state_t;

#define ENUM_ERR_NONE      0x00u
#define ENUM_ERR_COLLISION 0x06u  /* Fehlercode 0x06, Abschnitt 6.4 */

/* T_enum-Grenzen aus Abschnitt 4.5.2 (1..60 s). */
#define ENUM_T_ENUM_MIN_S 1u
#define ENUM_T_ENUM_MAX_S 60u

typedef struct {
    enum_state_t state;
    uint8_t  address;         /* aktuelle Busadresse; 0 = keine */
    bool     is_service_addr; /* address == 250 durch Rueckfall, nicht durch Zuweisung */
    uint8_t  eeprom_address;  /* Spiegel EEPROM-Byte 4; 0 = nie enumeriert */
    uint16_t t_enum_ms;       /* Timeout in ms, aus EEPROM-Byte 5 */
    uint16_t elapsed_ms;      /* seit dem letzten ENUM_RESET */
    bool     chain_out_active;
    uint8_t  last_error;

    /* Ausgaben: nach jedem Schritt pruefen, ausfuehren, dann quittieren. */
    bool     want_eeprom_write; /* address nach EEPROM-Byte 4 schreiben */
    bool     want_ack;          /* ENUM_ASSIGN mit einem ACK bestaetigen */
} enum_fsm_t;

/*
 * Initialisiert den Automaten beim Start (Zustand INIT der Firmware).
 *   eeprom_address    EEPROM-Byte 4 (0 = noch nie enumeriert, sonst 1..250)
 *   t_enum_seconds    EEPROM-Byte 5, wird auf 1..60 begrenzt
 *
 * Ergebnis: Liegt eine gueltige EEPROM-Adresse vor, startet die Karte direkt
 * ADDRESSED unter dieser Adresse (Betrieb nach kurzem Spannungsausfall ohne
 * erneute Enumeration). Sonst UNADDRESSED.
 */
void enum_fsm_init(enum_fsm_t *fsm, uint8_t eeprom_address, uint8_t t_enum_seconds);

/*
 * Verarbeitet einen empfangenen, CRC-geprueften Rahmen.
 *   chain_in_active   Pegel der CHAIN_IN-Leitung im Moment des Empfangs
 * Nicht-Enumerationskommandos aendern den Zustand nicht.
 */
void enum_fsm_on_frame(enum_fsm_t *fsm, uint8_t cmd, uint8_t addr,
                       const uint8_t *payload, uint8_t payload_len,
                       bool chain_in_active);

/* Zeitfortschritt in Millisekunden. Loest bei Ablauf von T_enum den Rueckfall aus. */
void enum_fsm_on_tick(enum_fsm_t *fsm, uint16_t dt_ms);

/*
 * Meldet, dass ein zurueckgelesenes Sendebyte vom gesendeten abweicht
 * (Kollision, Abschnitt 4.5.3). Verwirft die Laufzeitadresse, Zustand
 * UNADDRESSED, Fehlercode 0x06. Die Mechanik bleibt unberuehrt (nicht Sache
 * dieses Automaten).
 */
void enum_fsm_on_echo_mismatch(enum_fsm_t *fsm);

/* Aktuelle Busadresse (0 = keine). */
uint8_t enum_fsm_address(const enum_fsm_t *fsm);

/*
 * Soll die Karte auf einen Rahmen mit Zieladresse frame_addr eine Antwort
 * senden? Broadcast (0) ist immer "ja" (das Kommando entscheidet dann selbst);
 * eine Unicast-Adresse nur im Zustand ADDRESSED bei exakter Uebereinstimmung.
 */
bool enum_fsm_responds_to(const enum_fsm_t *fsm, uint8_t frame_addr);

/* Setzt die Ausgabeflags zurueck, nachdem der Aufrufer sie behandelt hat. */
void enum_fsm_clear_outputs(enum_fsm_t *fsm);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_ENUMERATION_H */
