/*
 * Anwendungslogik der Zentralsteuerung: Betriebsarten, Zeichenabbildung auf die
 * Module, Auto-Rueckfall der Sekundenanzeige. Bindet lib/charmap, lib/clocktext
 * und lib/busmaster zusammen.
 *
 * Hardwareunabhaengig: die REST- und MQTT-Schicht in src/ ruft nur diese
 * Funktionen auf. Auf dem Host gegen einen simulierten Bus testbar.
 *
 * Grundlage: docs/spezifikation.md 7.3, 7.4, 7.5, 7.7.
 */
#ifndef KRONE_MASTERAPP_H
#define KRONE_MASTERAPP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "busmaster.h"
#include "charmap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_MODE_TEXT = 0,
    APP_MODE_CLOCK_HM,
    APP_MODE_CLOCK_HMS,
    APP_MODE_BLANK,
    APP_MODE_OFF,
} app_mode_t;

#define APP_TEXT_MAX 48u
#define APP_HMS_TIMEOUT_DEFAULT_MS 600000u  /* 10 min, Vorgabe (7.7) */

/* Obergrenzen fuer masterapp_status_json (Zeichen ohne NUL): Kopf mit
 * vollstaendig maskiertem Text (je Byte hoechstens \u00XX), je Modul ein
 * Eintrag mit Hoechstwerten, Abschluss. Per Host-Test gegen den
 * tatsaechlichen Worst Case geprueft. */
#define MASTERAPP_JSON_HEAD_MAX   (176u + 6u * APP_TEXT_MAX)
#define MASTERAPP_JSON_MODULE_MAX 160u
#define MASTERAPP_STATUS_JSON_MAX(n) \
    ((size_t)MASTERAPP_JSON_HEAD_MAX + (size_t)(n) * MASTERAPP_JSON_MODULE_MAX + 4u)

typedef struct {
    busmaster_t    *bus;
    uint8_t         module_count;   /* Feldbreite, hoechstens BUSMASTER_MAX_MODULES */
    app_mode_t      mode;
    char            sep;            /* Uhr-Trennzeichen ('.' oder '-') */
    charmap_align_t align;
    char            text[APP_TEXT_MAX + 1];

    uint32_t hms_timeout_ms;
    uint32_t hms_since_ms;

    bool     time_valid;
    uint8_t  hh, mm, ss;

    uint8_t  shown[BUSMASTER_MAX_MODULES];
    bool     have_shown;            /* false = beim naechsten Tick neu senden */
} masterapp_t;

void masterapp_init(masterapp_t *app, busmaster_t *bus, uint8_t module_count);

/* Betriebsart setzen. sep und align gelten fuer die Uhr bzw. den Text.
 * Die Anzeige wird beim naechsten Tick neu gesendet, auch ohne Aenderung. */
void masterapp_set_mode(masterapp_t *app, app_mode_t mode, char sep,
                        charmap_align_t align, uint32_t now_ms);

/* Freitext setzen (UTF-8, auf APP_TEXT_MAX Byte an einer Zeichengrenze
 * gekuerzt); schaltet die Betriebsart auf Text und sendet neu, auch wenn
 * derselbe Text schon angezeigt wird. */
void masterapp_set_text(masterapp_t *app, const char *text, uint32_t now_ms);

/* Aktuelle Ortszeit einspeisen (aus NTP). */
void masterapp_set_time(masterapp_t *app, uint8_t hh, uint8_t mm, uint8_t ss);
void masterapp_time_invalid(masterapp_t *app);

/*
 * Regelmaessig aufrufen. Berechnet die Zielanzeige der aktuellen Betriebsart
 * und reiht bei Aenderung SET_ALL + GO ein (busmaster_show). Behandelt den
 * Auto-Rueckfall CLOCK_HMS -> CLOCK_HM nach hms_timeout_ms.
 *
 * Laeuft auch waehrend einer Enumeration: der Inhalt wartet dann in der
 * Sendewarteschlange des busmaster (neuester Stand gewinnt) und geht erst
 * nach deren Abschluss hinaus, stoert die Enumeration also nicht.
 *
 * Betriebsart "Aus": nichts senden und das Soll im busmaster verwerfen (kein
 * Soll/Ist-Abgleich); beim Verlassen wird die Anzeige neu gesendet.
 */
void masterapp_tick(masterapp_t *app, uint32_t now_ms);

/* Zielblaetter der aktuellen Anzeige (module_count Werte). Fuer Tests / UI. */
void masterapp_current_blaetter(const masterapp_t *app, uint8_t *out);

/*
 * /api/status als JSON, Text und Trennzeichen JSON-konform maskiert.
 * Rueckgabe: geschriebene Laenge (ohne NUL); 0, wenn out_size nicht reicht.
 * out ist in jedem Fall NUL-terminiert (bei 0 mit unvollstaendigem Inhalt).
 *
 * Felder: mode, sep, text, time_valid, align, detected, enum_busy,
 * warn (BM_WARN_*), modules[] mit addr, online, ist, ziel (vom Modul gemeldet),
 * soll (vom Master gesetzt, 0 = keins), state, error, corr, blatt, fw, miss,
 * coll (gemeldete 0x06), uidw (Bit 0 UID-Dublette, Bit 1 UID gewechselt).
 */
size_t masterapp_status_json(const masterapp_t *app, char *out, size_t out_size);

/* Benoetigte Laenge (ohne NUL) fuer den aktuellen Zustand. */
size_t masterapp_status_json_len(const masterapp_t *app);

/* Puffergroesse (mit NUL), die fuer n Module in jedem Zustand reicht. */
size_t masterapp_status_json_max(uint8_t module_count);

/* Betriebsart als stabiler Bezeichner ("text", "clock_hm", ...). */
const char *masterapp_mode_name(app_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_MASTERAPP_H */
