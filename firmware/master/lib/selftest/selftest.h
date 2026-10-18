/*
 * Selbsttest der Anzeige, siehe docs/spezifikation.md 7.3 (#47).
 *
 * Jedes Modul faehrt auf SELFTEST eine volle Umdrehung, zaehlt die
 * Blattimpulse zwischen zwei Leerbildern und misst dabei die Umlaufzeit sowie
 * den kuerzesten und laengsten Blattabstand. Der Master sammelt die Ergebnisse
 * per GET_TEST ein und bewertet sie gegen die Sollzeit je Blatt.
 *
 * Hardwareunabhaengig ueber die busmaster-API, auf dem Host getestet.
 */
#ifndef KRONE_SELFTEST_H
#define KRONE_SELFTEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "busmaster.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SELFTEST_MS_PER_BLATT  60u     /* Faktenblatt, wie MOTION_TIME_PER_BLATT_MS */
#define SELFTEST_TOL_PCT       20u     /* zulaessige Abweichung der Umlaufzeit */
#define SELFTEST_STEP_MAX_MS   120u    /* laengster Blattabstand: 2 x Soll */
#define SELFTEST_SETTLE_MS     1500u   /* erst danach Ergebnisse abfragen */
#define SELFTEST_POLL_MS       500u    /* je Modul hoechstens so oft GET_TEST */
#define SELFTEST_MAX_POLLS     4u      /* ohne jede Antwort: keine Antwort */
#define SELFTEST_TIMEOUT_MS    25000u  /* 2 x Homing bei 80 Blatt + Reserve */

typedef enum {
    ST_RES_NONE = 0,     /* nicht getestet */
    ST_RES_OK,
    ST_RES_NO_ANSWER,    /* keine GET_TEST-Antwort (offline oder Firmware < 1.19) */
    ST_RES_INCOMPLETE,   /* keine volle Umdrehung, Fehlercode des Moduls dabei */
    ST_RES_BLATTZAHL,    /* erkannte Blattzahl unplausibel oder ungleich Konfiguration */
    ST_RES_TIMING,       /* Umlaufzeit oder Blattabstand weicht ab */
} selftest_result_t;

typedef struct {
    bool     running;
    bool     done;                              /* Ergebnis eines Laufs liegt vor */
    uint8_t  count;                             /* getestete Module 1..count */
    uint32_t start_ms;
    uint32_t last_poll_ms[BUSMASTER_MAX_MODULES];
    uint8_t  polls[BUSMASTER_MAX_MODULES];
    uint8_t  result[BUSMASTER_MAX_MODULES];     /* selftest_result_t */
} selftest_t;

void selftest_init(selftest_t *st);

/* Startet den Test fuer die Module 1..count (SELFTEST als Broadcast).
 * false, wenn schon einer laeuft oder count 0 ist. */
bool selftest_start(selftest_t *st, busmaster_t *bm, uint8_t count, uint32_t now_ms);

/* Fortschritt: GET_TEST je Modul, Abschluss nach Ergebnis aller Module oder
 * SELFTEST_TIMEOUT_MS. true genau in dem Aufruf, der den Lauf abschliesst. */
bool selftest_tick(selftest_t *st, busmaster_t *bm, uint32_t now_ms);

/* Bewertung eines Moduls aus seinen test_*-Feldern. */
selftest_result_t selftest_evaluate(const bm_module_t *m);

/* Kurzname fuer UI/Log/MQTT: "ok", "keine_antwort", ... */
const char *selftest_result_name(selftest_result_t r);

/*
 * {"running":..,"done":..,"modules":[{"addr":1,"res":"ok","blatt":40,
 *  "rot":2400,"min":58,"max":63,"err":0}, ...]}
 * Rueckgabe: Laenge ohne NUL, 0 bei zu kleinem Puffer.
 */
size_t selftest_json(const selftest_t *st, const busmaster_t *bm, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_SELFTEST_H */
