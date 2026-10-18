/*
 * Bewegungs-Zustandsautomat der Modulsteuerung.
 *
 * Hardwareunabhaengig: kennt weder Portpins noch Zeitgeber. Der Aufrufer
 * liefert eine monotone Millisekundenzeit und die entprellten Ereignisse,
 * fragt danach das Triac-Gate und den Status ab.
 *
 * Grundlage: docs/spezifikation.md
 *   6.1  Zustandsautomat (HOMING / IDLE / MOVING / ERROR)
 *   6.2  Ablauf: Homing, Blattzahlerkennung, MOVING, Abschaltvorhalt
 *   6.4  Fehlercodes 0x01..0x05
 *   4.3  Auswertende Flanke fallend, 20 ms Sperrzeit
 *   5.4  SET/SET_ALL puffern das Ziel, erst GO loest die Bewegung aus
 *   Faktenblatt: 60 ms je Blatt, Weg = (Ziel - Ist) mod Blattzahl, nur vorwaerts
 */
#ifndef KRONE_MOTION_H
#define KRONE_MOTION_H

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Zeitkonstanten in Millisekunden (Spezifikation 4.3, 6.2, 6.4). */
#define MOTION_DEBOUNCE_MS        20u    /* Sperrzeit nach einer Flanke */
#define MOTION_TIME_PER_BLATT_MS  60u
/* Homing-Timeout (0x02) und Laufzeitgrenze (0x05) fuer 40 Blatt, rund 1,7
 * Umdrehungen. Fuer 64/80 Blatt skalieren beide mit der Blattzahl, siehe
 * motion_homing_timeout_ms() / motion_runtime_limit_ms(). */
#define MOTION_HOMING_TIMEOUT_MS  4000u  /* Fehlercode 0x02 */
#define MOTION_NO_PULSE_MS        200u   /* Fehlercode 0x01, mit Wiederholung */
#define MOTION_RUNTIME_LIMIT_MS   4000u  /* Fehlercode 0x05, Laufzeitueberwachung */
#define MOTION_MAX_RETRIES        3u     /* wie die Originalsteuerung */
/* So viele Blattimpulse bei abgeschaltetem Motor (IDLE/ERROR) innerhalb des
 * Fensters gelten als Motorlauf ohne Ansteuerung (0x07); ein einzelner
 * Nachlaufimpuls nach dem Abschalten loest nichts aus. */
#define MOTION_STRAY_PULSES       3u
#define MOTION_STRAY_WINDOW_MS    1000u

/* Selbsttest (Spez. 7.3, #47): Zustand in GET_TEST-Byte 0. */
#define MOTION_TEST_NONE     0u   /* seit dem Start nicht gelaufen */
#define MOTION_TEST_RUNNING  1u
#define MOTION_TEST_DONE     2u   /* Ergebnis gueltig, Umlaufzeit 0 = nicht vollendet */

/* Fehlercodes, Abschnitt 6.4. */
#define MOTION_ERR_NONE        0x00u
#define MOTION_ERR_NO_BLATT    0x01u
#define MOTION_ERR_NO_LEER     0x02u
#define MOTION_ERR_BLATTZAHL   0x03u
#define MOTION_ERR_UNSYNCED    0x04u
#define MOTION_ERR_RUNTIME     0x05u
/* 0x06 (Sendeecho) meldet die Enumeration, siehe enumeration.h. */
#define MOTION_ERR_STRAY       0x07u   /* Blattimpulse ohne Motoransteuerung */

typedef enum {
    MOTION_HOMING = 0,
    MOTION_IDLE,
    MOTION_MOVING,
    MOTION_ERROR,
} motion_state_t;

typedef struct {
    motion_state_t state;

    uint8_t  blattzahl;
    uint8_t  blatt_offset;
    uint8_t  abschaltvorhalt_ms;
    bool     position_save;

    uint8_t  current;             /* Ist-Blatt 1..blattzahl; 0 = unbekannt */
    uint8_t  target;              /* gepuffertes Ziel (SET/SET_ALL, Statusbyte 1) */
    uint8_t  active_target;       /* Ziel der laufenden Fahrt, von GO uebernommen */
    bool     go_pending;          /* GO in MOVING/HOMING, wird in IDLE ausgefuehrt */
    bool     synced;
    uint8_t  error;
    uint8_t  detected_blattzahl;  /* 0 = noch nicht erkannt */
    uint16_t correction_count;

    /* intern */
    uint32_t last_blatt_ms;       /* Bezug fuer 0x01 und Abschaltvorhalt */
    uint32_t blatt_edge_ms;       /* letzte angenommene Blattflanke (Sperrzeit) */
    uint32_t last_leer_ms;        /* letzte angenommene Leerbildflanke (Sperrzeit) */
    uint32_t motor_since_ms;
    uint32_t state_since_ms;
    uint8_t  retries;
    bool     counting;            /* Blattzahlerkennung laeuft */
    uint8_t  count_value;
    bool     seen_first_leer;
    bool     motor_on;            /* Gate-Pegel PA7 (high = Motor an) */
    bool     gate_cut_early;      /* Abschaltvorhalt hat das Gate abgeschaltet */
    uint8_t  stray_count;         /* Blattimpulse bei abgeschaltetem Motor */
    uint32_t stray_since_ms;

    /* Selbsttest (#47) */
    uint8_t  test_state;          /* MOTION_TEST_* */
    uint16_t test_rot_ms;         /* Leerbild bis Leerbild, 0 = nicht vollendet */
    uint8_t  test_min_ms;         /* kuerzester / laengster Abstand zweier Blattimpulse */
    uint8_t  test_max_ms;         /* (auf 255 begrenzt) */
    uint32_t test_leer_ms;
    uint32_t test_prev_ms;        /* letzter gemessener Blattimpuls, 0 = keiner */
} motion_t;

/*
 * Startet den Automaten. stored_position ist die zuletzt gespeicherte Blattlage
 * (0 = keine). Homing laeuft, wenn keine Position vorliegt, die
 * Positionsspeicherung aus ist oder das Autohoming-Flag gesetzt ist.
 */
void motion_init(motion_t *m, const module_config_t *cfg,
                 uint8_t stored_position, uint32_t now_ms);

/*
 * SET_CONFIG zur Laufzeit (#23). Vorhalt und Positionsspeicherung gelten
 * sofort. Aendern sich Blattzahl oder Offset, stimmt die Blattzaehlung nicht
 * mehr: Motor aus, IDLE, nicht synchronisiert, Fehler 0x04. Die Karte laeuft
 * nicht selbsttaetig an; HOME synchronisiert neu.
 */
void motion_apply_config(motion_t *m, const module_config_t *cfg, uint32_t now_ms);

/* Entprellte fallende Flanke am Blatt-Impuls. Rueckgabe: true, wenn angenommen. */
bool motion_on_blatt_pulse(motion_t *m, uint32_t now_ms);

/* Entprellte fallende Flanke am Leerbild-Impuls. Rueckgabe: true, wenn angenommen. */
bool motion_on_leer_pulse(motion_t *m, uint32_t now_ms);

/* SET / SET_ALL: Ziel puffern (1..blattzahl). Aendert eine laufende Fahrt
 * nicht. Rueckgabe: true bei gueltigem Wert. */
bool motion_set_target(motion_t *m, uint8_t blatt);

/* GO: in IDLE Fahrt zum gepufferten Ziel starten. In MOVING/HOMING wird das GO
 * vorgemerkt und beim Erreichen von IDLE ausgefuehrt. Im Fehlerzustand nur
 * Rueckkehr nach IDLE bzw. Homing, keine Fahrt (Diagramm 6.1: ERROR --GO--> IDLE). */
void motion_go(motion_t *m, uint32_t now_ms);

/* STOP: Motor aus, MOVING/HOMING -> IDLE, vorgemerktes GO verworfen. */
void motion_stop(motion_t *m, uint32_t now_ms);

/* HOME: erzwungenes Homing, vorgemerktes GO verworfen. */
void motion_home(motion_t *m, uint32_t now_ms);

/* Sofort in ERROR mit Motor aus, z. B. nach einem Watchdog-Reset (#19). */
void motion_enter_error(motion_t *m, uint8_t code, uint32_t now_ms);

/* Zeitfortschritt: Timeouts, Laufzeitueberwachung, Abschaltvorhalt. */
void motion_tick(motion_t *m, uint32_t now_ms);

/* Selbsttest (Spez. 7.3): erzwingt ein Homing mit Blattzahlerkennung und misst
 * dabei die Umlaufzeit zwischen den beiden Leerbildern sowie den kuerzesten und
 * laengsten Abstand zweier Blattimpulse. Endet mit dem zweiten Leerbild, einem
 * Fehler, STOP, HOME oder einer Geometrieaenderung. */
void motion_selftest(motion_t *m, uint32_t now_ms);

/* GET_TEST-Antwort: Zustand, erkannte Blattzahl, Umlaufzeit (16 Bit, low
 * zuerst), kuerzester und laengster Blattabstand in ms, Fehlercode. */
void motion_fill_test(const motion_t *m, uint8_t out[7]);

/* Homing-Timeout (0x02) fuer die konfigurierte Blattzahl: 4 s bei 40 Blatt,
 * 6,4 s bei 64, 8 s bei 80 (je rund 1,7 Umdrehungen). Gilt bis zum ersten
 * Leerbild und erneut fuer die Blattzahlerkennung bis zum zweiten. */
uint32_t motion_homing_timeout_ms(const motion_t *m);

/* Laufzeitgrenze (0x05) fuer durchgehenden Motorlauf im aktuellen Zustand:
 * Fahrt wie der Homing-Timeout, Homing (beide Phasen) das Doppelte. */
uint32_t motion_runtime_limit_ms(const motion_t *m);

/* Pegel fuer PA7 (Triac-Gate). High = Motor an, in jeder Bestueckung (#18). */
bool motion_triac_gate(const motion_t *m);

/* Zustandscode fuer Statusbyte 2 (0 Idle, 1 Homing, 2 Moving, 3 Fehler). */
uint8_t motion_state_code(const motion_t *m);

/* Fuellt die 8-Byte-Statusantwort nach Abschnitt 5.5. */
void motion_fill_status(const motion_t *m, uint8_t out[8], uint8_t fw_version);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_MOTION_H */
