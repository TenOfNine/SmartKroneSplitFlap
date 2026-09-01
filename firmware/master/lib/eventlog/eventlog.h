/*
 * Kleiner Ereignis-Ringpuffer der Zentralsteuerung fuer die Web-UI
 * (Tab "Log"). Haelt die letzten EVLOG_CAPACITY Meldungen im RAM.
 *
 * Hardwareunabhaengig: die Firmware ruft evlog_push() bei WLAN-, MQTT-, NTP-
 * und Busereignissen auf; src/ serviert evlog_json() unter /api/log.
 * Auf dem Host getestet (test/test_eventlog).
 *
 * Es gibt keine gepufferte Uhr -- die Zeitstempel sind Millisekunden seit
 * Systemstart (der Aufrufer uebergibt millis()).
 */
#ifndef KRONE_EVENTLOG_H
#define KRONE_EVENTLOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EVLOG_INFO = 0,
    EVLOG_WARN = 1,
    EVLOG_ERR  = 2,
} evlog_sev_t;

#define EVLOG_CAPACITY 32u
#define EVLOG_SRC_MAX   8u   /* inkl. NUL: "sys","wifi","ntp","bus","mqtt","ota" */
#define EVLOG_MSG_MAX  76u   /* inkl. NUL */

typedef struct {
    uint32_t    t_ms;
    evlog_sev_t sev;
    char        src[EVLOG_SRC_MAX];
    char        msg[EVLOG_MSG_MAX];
} evlog_entry_t;

typedef struct {
    evlog_entry_t buf[EVLOG_CAPACITY];
    uint16_t      head;    /* Index des naechsten Schreibplatzes */
    uint16_t      count;   /* belegte Eintraege, <= EVLOG_CAPACITY */
    uint32_t      seq;     /* fortlaufende Nummer aller je gepushten Eintraege */
} evlog_t;

void evlog_init(evlog_t *lg);

/* Meldung anhaengen. fmt/... wie printf; die Nachricht wird auf EVLOG_MSG_MAX
 * gekuerzt. Ist der Puffer voll, faellt der aelteste Eintrag heraus. */
void evlog_push(evlog_t *lg, uint32_t t_ms, evlog_sev_t sev,
                const char *src, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 5, 6)))
#endif
    ;

void evlog_clear(evlog_t *lg);

/*
 * JSON nach out schreiben:
 *   {"seq":<gesamtzahl>,"entries":[
 *      {"t":<ms>,"sev":0|1|2,"src":"..","msg":".."}, ... ]}
 * Neueste zuerst. min_sev filtert (EVLOG_INFO = alle, EVLOG_WARN = Warnungen +
 * Fehler, EVLOG_ERR = nur Fehler).
 * Rueckgabe: geschriebene Laenge ohne NUL, oder 0 bei Platzmangel.
 */
size_t evlog_json(const evlog_t *lg, evlog_sev_t min_sev,
                  char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_EVENTLOG_H */
