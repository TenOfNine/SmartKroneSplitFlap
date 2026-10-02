/*
 * Entscheidungslogik des residenten Bootloaders (firmware/bootloader).
 *
 * Hardwareunabhaengig, auf dem Host testbar. Der Bootloader reicht empfangene
 * Rahmen und Zeitablaeufe hierher und fuehrt nur die zurueckgegebene Aktion aus
 * (Antwort senden, Neustart, Sprung in die App). Flash-Seiten und das
 * EEPROM-Byte "App gueltig" schreibt er ueber Callbacks.
 *
 * Regeln (Spez. 5.7, docs/module-bootloader.md):
 *  - Startbar ist die App nur, wenn ihr erstes Wort beschrieben ist und der
 *    Marker FWBOOT_MARKER_VALID (Bus-Update fertig) oder FWBOOT_MARKER_BLANK
 *    (geloeschtes EEPROM nach Werksflash) traegt.
 *  - Ein gueltiges FW_BEGIN setzt den Marker auf FWBOOT_MARKER_UPDATE, bevor
 *    irgendeine Seite geschrieben wird. Startbar wird die App erst wieder nach
 *    einem erfolgreichen FW_END. Ein Abbruch fuehrt nie in die App.
 *  - Angenommen werden nur Unicast-Rahmen an die eigene Adresse (bei
 *    ungueltiger EEPROM-Adresse: Serviceadresse) mit passender Nutzlastlaenge.
 *  - Die Uebertragung ist wiederaufsetzbar: Wiederholungen des letzten
 *    Bruchstuecks werden bestaetigt (lib/fwupdate), FW_BEGIN startet jederzeit
 *    neu, Leerlauf beendet sie nicht.
 *
 * Kein Registerzugriff, kein dynamischer Speicher.
 */
#ifndef KRONE_FWBOOT_H
#define KRONE_FWBOOT_H

#include <stdbool.h>
#include <stdint.h>

#include "fwupdate.h"

#ifdef __cplusplus
extern "C" {
#endif

/* EEPROM-Byte "App gueltig" (Lage: firmware/bootloader/src/board.h). */
#define FWBOOT_MARKER_UPDATE 0x00u  /* Uebertragung begonnen, App unvollstaendig */
#define FWBOOT_MARKER_VALID  0xA5u  /* Bus-Update erfolgreich abgeschlossen      */
#define FWBOOT_MARKER_BLANK  0xFFu  /* geloeschtes EEPROM (Werksflash)            */

/* Zeiten (ms). Der Master (lib/moduleupdate, T_APP) wartet laenger als das
 * Startfenster, bevor er die neue App per GET_VERSION bestaetigt. */
#define FWBOOT_WINDOW_MS 2500u  /* Startfenster bei gueltiger App ohne Marker */
#define FWBOOT_IDLE_MS   3000u  /* Ruhe bis zum App-Start nach ENTER_BOOTLOADER */

typedef enum {
    FWBOOT_IGNORE = 0,   /* nicht fuer uns: weiter warten, Zeitablauf laeuft weiter */
    FWBOOT_NONE,         /* angenommen, keine Antwort; Zeitablauf neu starten       */
    FWBOOT_REPLY,        /* reply_cmd/reply[] an addr senden                       */
    FWBOOT_REPLY_RESET,  /* Antwort senden, danach Neustart (Update fertig)        */
    FWBOOT_START_APP,    /* in die App springen                                    */
} fwboot_action_t;

typedef void (*fwboot_set_marker_fn)(void *ctx, uint8_t marker);

typedef struct {
    fwupdate_t             fu;
    fwupdate_write_page_fn write_page;
    fwboot_set_marker_fn   set_marker;
    void    *ctx;
    uint32_t app_max;     /* groesstes zulaessiges Abbild in Byte */
    uint8_t  addr;        /* Adresse, auf die der Bootloader hoert und antwortet */
    uint8_t  bl_version;  /* Byte 4 der GET_VERSION-Antwort */
    bool     app_valid;   /* aktueller Stand, nicht der beim Start */
    bool     window;      /* Startfenster laeuft */
    bool     active;      /* Uebertragung laeuft (nach gueltigem FW_BEGIN) */
    uint8_t  reply_cmd;
    uint8_t  reply_len;
    uint8_t  reply[5];
} fwboot_t;

/* Startbarkeit aus erstem App-Wort und Marker-Byte. */
bool fwboot_app_valid(uint16_t first_word, uint8_t marker);

/* forced = Marker "im Bootloader bleiben" (CMD_ENTER_BOOTLOADER) war gesetzt. */
void fwboot_init(fwboot_t *bt, uint8_t eeprom_addr, bool forced, bool app_valid,
                 uint32_t app_max, uint8_t bl_version,
                 fwupdate_write_page_fn write_page, fwboot_set_marker_fn set_marker,
                 void *ctx);

/* Zeitablauf ab der letzten angenommenen Aktion (alles ausser FWBOOT_IGNORE). */
uint16_t fwboot_timeout_ms(const fwboot_t *bt);

fwboot_action_t fwboot_on_timeout(fwboot_t *bt);

/* Ein CRC-gepruefter Rahmen vom Bus. */
fwboot_action_t fwboot_on_frame(fwboot_t *bt, uint8_t cmd, uint8_t addr,
                                const uint8_t *payload, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_FWBOOT_H */
