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
#define MOTION_HOMING_TIMEOUT_MS  4000u  /* Fehlercode 0x02 */
#define MOTION_NO_PULSE_MS        200u   /* Fehlercode 0x01, mit Wiederholung */
#define MOTION_RUNTIME_LIMIT_MS   4000u  /* Fehlercode 0x05, Laufzeitueberwachung */
#define MOTION_MAX_RETRIES        3u     /* wie die Originalsteuerung */

/* Fehlercodes, Abschnitt 6.4. */
#define MOTION_ERR_NONE        0x00u
#define MOTION_ERR_NO_BLATT    0x01u
#define MOTION_ERR_NO_LEER     0x02u
#define MOTION_ERR_BLATTZAHL   0x03u
#define MOTION_ERR_UNSYNCED    0x04u
#define MOTION_ERR_RUNTIME     0x05u

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
    bool     triac_invert;
    bool     position_save;

    uint8_t  current;             /* Ist-Blatt 1..blattzahl; 0 = unbekannt */
    uint8_t  target;              /* gepuffertes Ziel-Blatt 1..blattzahl */
    bool     synced;
    uint8_t  error;
    uint8_t  detected_blattzahl;  /* 0 = noch nicht erkannt */
    uint16_t correction_count;

    /* intern */
    uint32_t last_blatt_ms;
    uint32_t last_leer_ms;
    uint32_t motor_since_ms;
    uint32_t state_since_ms;
    uint8_t  retries;
    bool     counting;            /* Blattzahlerkennung laeuft */
    uint8_t  count_value;
    bool     seen_first_leer;
    bool     motor_on;            /* logischer Gate-Wunsch, vor Invertierung */
    bool     gate_cut_early;      /* Abschaltvorhalt hat das Gate abgeschaltet */
} motion_t;

/*
 * Startet den Automaten. stored_position ist die zuletzt gespeicherte Blattlage
 * (0 = keine). Homing laeuft, wenn keine Position vorliegt, die
 * Positionsspeicherung aus ist oder das Autohoming-Flag gesetzt ist.
 */
void motion_init(motion_t *m, const module_config_t *cfg,
                 uint8_t stored_position, uint32_t now_ms);

/* Entprellte fallende Flanke am Blatt-Impuls. Rueckgabe: true, wenn angenommen. */
bool motion_on_blatt_pulse(motion_t *m, uint32_t now_ms);

/* Entprellte fallende Flanke am Leerbild-Impuls. Rueckgabe: true, wenn angenommen. */
bool motion_on_leer_pulse(motion_t *m, uint32_t now_ms);

/* SET / SET_ALL: Ziel puffern (1..blattzahl). Rueckgabe: true bei gueltigem Wert. */
bool motion_set_target(motion_t *m, uint8_t blatt);

/* GO: Bewegung zum gepufferten Ziel starten. Loest im Fehlerzustand die
 * Rueckkehr nach IDLE aus (Diagramm 6.1: ERROR --GO/SET--> IDLE). */
void motion_go(motion_t *m, uint32_t now_ms);

/* STOP: Motor aus, MOVING -> IDLE. */
void motion_stop(motion_t *m, uint32_t now_ms);

/* HOME: erzwungenes Homing. */
void motion_home(motion_t *m, uint32_t now_ms);

/* Zeitfortschritt: Timeouts, Laufzeitueberwachung, Abschaltvorhalt. */
void motion_tick(motion_t *m, uint32_t now_ms);

/* Elektrischer Pegel fuer PA7 (Triac-Gate), inkl. Invertierung nach Flag Bit 2. */
bool motion_triac_gate(const motion_t *m);

/* Zustandscode fuer Statusbyte 2 (0 Idle, 1 Homing, 2 Moving, 3 Fehler). */
uint8_t motion_state_code(const motion_t *m);

/* Fuellt die 8-Byte-Statusantwort nach Abschnitt 5.5. */
void motion_fill_status(const motion_t *m, uint8_t out[8], uint8_t fw_version);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_MOTION_H */
