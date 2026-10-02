/*
 * Master-Seite des Busprotokolls, siehe docs/spezifikation.md 4.5, 5.
 *
 * Hardwareunabhaengig: der Aufrufer stellt eine Sende-Funktion und optional
 * eine Uhr bereit und fuettert empfangene Bytes ein. Die CHAIN-Leitung wird
 * ueber ein Flag angefordert. Auf dem Host gegen einen simulierten Bus testbar.
 *
 * Sendedisziplin (#20, #21): Ausser den Abfragen busmaster_poll_status/
 * _poll_version und busmaster_service_poll sendet nur busmaster_tick, und nur
 * bei freiem Bus: keine Antwort ausstehend, keine Enumeration (ausser deren
 * eigenen Schritten), Busruhe seit dem letzten empfangenen Byte, Parser nicht
 * mitten in einem Rahmen. Kommandos aus Web/MQTT/Anwendung gehen in eine
 * kleine Warteschlange.
 *
 * Zeitbasis: Mit busmaster_set_clock liest der busmaster die Zeit selbst; das
 * Sendeende (nach tx(), das bis zum Ende der Uebertragung blockiert) ist dann
 * der Bezug fuer Timeout und Pausen, uebergebene Zeitstempel werden ignoriert.
 * Ohne Uhr (Host-Tests) gilt der uebergebene Zeitstempel. Alle Zeiten in ms,
 * Vergleiche ueberlaufsicher.
 *
 * Nutzt lib/protocol (mit der Modul-Firmware geteilt).
 */
#ifndef KRONE_BUSMASTER_H
#define KRONE_BUSMASTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUSMASTER_MAX_MODULES 32u

/* --- Zeitverhalten (Spez. 5.6) ------------------------------------- */
/* Antwort-Timeout ab Rahmenende. Vergleich strikt groesser: im 1-ms-Raster
 * also mindestens 5 ms echte Zeit. */
#define BUSMASTER_TIMEOUT_MS          5u
#define BUSMASTER_RETRIES             2u
/* Busruhe vor jeder Sendung: im 1-ms-Raster mindestens 1 ms echte Ruhe. */
#define BUSMASTER_QUIET_MS            2u
/* Obergrenze fuer das Verschieben einer Wiederholung bei belegtem Bus;
 * danach gilt die Anfrage als gescheitert (keine Sendung in den Verkehr). */
#define BUSMASTER_RETRY_DEFER_MAX_MS  20u
/* Bleibt der Rest eines angefangenen Rahmens so lange aus, wird der Parser
 * zurueckgesetzt (abgerissener Rahmen blockiert den Bus sonst dauerhaft). */
#define BUSMASTER_FRAME_STALE_MS      5u
/* Nach einer Wiederholung kann noch die Antwort auf die fruehere Anfrage
 * kommen: so lange ab dem letzten Sendeende nichts Neues senden. */
#define BUSMASTER_LATE_GUARD_MS       (BUSMASTER_TIMEOUT_MS + 1u)

/* --- Enumeration (Spez. 4.5) --------------------------------------- */
#define BUSMASTER_ENUM_RESET_COUNT    2u   /* ENUM_RESET-Rahmen je Lauf      */
#define BUSMASTER_ENUM_RESET_GAP_MS   3u   /* Pause nach jedem ENUM_RESET    */

/* --- Abfrageplan --------------------------------------------------- */
#define BUSMASTER_POLL_INTERVAL_MS    100u    /* Status rundlaufend, Spez. 5.6 */
#define BUSMASTER_VERSION_POLL_MS     300u    /* eigener Takt GET_VERSION      */
#define BUSMASTER_VERSION_MAX_FAILS   3u      /* danach bis zur Enumeration aus */
#define BUSMASTER_SERVICE_PROBE_MS    30000u  /* PING an Adresse 250, Spez. 4.5.2 */

/* --- Soll/Ist-Abgleich (#26) --------------------------------------- */
#define BUSMASTER_RECONCILE_MIN_MS    2000u   /* je Modul hoechstens so oft */
#define BUSMASTER_RECONCILE_MAX_TRIES 3u      /* dann bis zur Soll-Aenderung aus */

#define BUSMASTER_QUEUE_LEN           8u
#define BUSMASTER_UID_LEN             10u     /* GET_UID, Spez. 5.4 */

/* Warnungen (busmaster_warnings), siehe Spez. 4.5.2-4.5.4. */
#define BM_WARN_UID_DUP      0x01u  /* gleiche UID auf zwei Adressen          */
#define BM_WARN_UID_CHANGED  0x02u  /* UID einer Adresse seit dem Vorlauf neu */
#define BM_WARN_SERVICE_ADDR 0x04u  /* Antwort/Kollision auf Adresse 250      */
#define BM_WARN_COLLISION    0x08u  /* ein Modul meldete Fehlercode 0x06      */
#define BM_WARN_ENUM_LIMIT   0x10u  /* mehr als BUSMASTER_MAX_MODULES Karten  */
#define BM_WARN_ENUM_GAP     0x20u  /* Kette vor der erwarteten Modulzahl zu
                                       Ende, Karten dahinter nachsondiert   */

typedef struct {
    bool     online;
    uint8_t  ist_blatt;
    uint8_t  ziel_blatt;  /* vom Modul gemeldetes (gepuffertes) Ziel */
    uint8_t  zustand;     /* 0 Idle, 1 Homing, 2 Moving, 3 Fehler */
    uint8_t  fehler;
    uint8_t  blattzahl;
    uint16_t korrektur;
    uint8_t  fw_version;
    uint8_t  miss_count;  /* nur GET_STATUS-Fehlschlaege */
    /* aus CMD_GET_VERSION (Firmware-Verteilung ueber den Bus) */
    uint16_t app_ver;      /* (major<<8)|minor, 0 = unbekannt   */
    uint8_t  ver_flags;    /* PROTO_VER_FLAG_*                    */
    bool     ver_known;
    uint8_t  ver_fails;    /* GET_VERSION ohne Antwort in Folge   */
    /* aus CMD_GET_CONFIG (Spezifikation 5.4 / 6.3) */
    uint8_t  cfg_blattzahl;
    uint8_t  cfg_offset;
    uint8_t  cfg_vorhalt;
    uint8_t  cfg_flags;
    bool     cfg_known;
    /* Soll/Ist-Abgleich (#26): Soll aus busmaster_show, getrennt vom Ziel */
    uint8_t  soll;
    bool     soll_valid;
    uint8_t  rc_tries;     /* Abgleichversuche seit Soll-Aenderung/Erfolg */
    bool     rc_stopped;   /* nach STOP bis zur naechsten Anzeige ausgesetzt */
    uint32_t rc_last_ms;
    /* Verifikationslauf (#28, Spez. 4.5.4) */
    uint8_t  uid[BUSMASTER_UID_LEN];
    bool     uid_known;    /* uid enthaelt eine gelesene Seriennummer */
    bool     uid_fresh;    /* im letzten Verifikationslauf gelesen    */
    bool     uid_dup;      /* dieselbe UID auch auf einer anderen Adresse */
    bool     uid_changed;  /* UID weicht vom vorigen Lauf ab          */
    uint16_t collisions;   /* gemeldete Fehlercodes 0x06 seit Start   */
} bm_module_t;

typedef enum {
    BM_ENUM_IDLE,
    BM_ENUM_REQUESTED,   /* angefordert, wartet auf freien Bus          */
    BM_ENUM_RESET_SENT,  /* ENUM_RESET gesendet, Pause                  */
    BM_ENUM_ASSIGNING,   /* ENUM_ASSIGN senden bzw. ACK abwarten        */
    BM_ENUM_CHECKING,    /* ACK ausgeblieben: PING an die Adresse       */
    BM_ENUM_FINISHING,   /* ENUM_DONE senden                            */
    BM_ENUM_PROBING,     /* Adressen hinter dem Kettenende per PING     */
    BM_ENUM_VERIFYING,   /* GET_UID je Adresse, danach PING an 250      */
    BM_ENUM_DONE,
} bm_enum_phase_t;

/* Art der ausstehenden Anfrage (bestimmt Antwortpruefung und Fehlschlag). */
typedef enum {
    BM_TX_NONE = 0,
    BM_TX_STATUS,       /* GET_STATUS, Fehlschlag zaehlt miss_count     */
    BM_TX_VERSION,      /* GET_VERSION, Fehlschlag zaehlt ver_fails     */
    BM_TX_CONFIG,       /* GET_CONFIG                                   */
    BM_TX_ACK,          /* Unicast-Kommando mit ACK (SET, STOP, HOME,
                           IDENTIFY, SET_CONFIG)                        */
    BM_TX_ENUM_ASSIGN,  /* ENUM_ASSIGN, ACK mit der vergebenen Adresse  */
    BM_TX_ENUM_PING,    /* PING nach ausgebliebenem ENUM_ASSIGN-ACK     */
    BM_TX_UID,          /* GET_UID im Verifikationslauf                 */
    BM_TX_SERVICE,      /* PING an die Serviceadresse 250               */
    BM_TX_ENUM_PROBE,   /* PING an eine Adresse hinter dem Kettenende   */
} bm_tx_kind_t;

/* Eintrag der Sendewarteschlange. */
typedef struct {
    uint8_t cmd;
    uint8_t addr;
    uint8_t len;
    uint8_t flags;   /* BMQ_* in busmaster.c */
    uint8_t payload[PROTO_MAX_PAYLOAD];
} bm_queue_entry_t;

/* Optionales Diagnose-Log fuer den awaiting/Retry/Timeout-Zustand (Issue #16).
 * event: "send" (Anfrage raus, awaiting=true), "match" (gueltige Antwort
 * erhalten), "retry" (Timeout, erneut gesendet), "defer" (Wiederholung
 * wegen laufenden Empfangs verschoben), "give_up" (endgueltig ohne Antwort).
 * sent_ms ist das Sendeende der letzten Uebertragung. */
typedef void (*busmaster_log_fn)(void *log_ctx, const char *event, uint8_t cmd,
                                 uint8_t addr, uint32_t now_ms, uint32_t sent_ms,
                                 uint8_t retries);

/* Uhr in ms (monoton, darf ueberlaufen). */
typedef uint32_t (*busmaster_clock_fn)(void *ctx);

typedef struct {
    void (*tx)(void *ctx, const uint8_t *data, size_t len);
    void  *tx_ctx;
    busmaster_log_fn   log_fn;   /* NULL = kein Logging (Default) */
    void              *log_ctx;
    busmaster_clock_fn now_fn;   /* NULL = uebergebene Zeitstempel */
    void              *now_ctx;

    uint8_t     module_count;    /* Ergebnis der letzten Enumeration */
    bm_module_t mod[BUSMASTER_MAX_MODULES];

    proto_parser_t parser;

    /* ausstehende Anfrage */
    bool     awaiting;
    uint8_t  pending_kind;       /* bm_tx_kind_t */
    uint8_t  pending_cmd;
    uint8_t  pending_addr;
    uint8_t  pending_payload[4];
    uint8_t  pending_len;
    uint8_t  pending_max_retries;
    uint32_t sent_ms;            /* Sendeende der letzten Uebertragung */
    uint8_t  retries;
    uint32_t txn_crc_snapshot;   /* crc_errors bei Beginn der Anfrage */
    bool     defer_active;
    uint32_t defer_since_ms;

    /* Busbelegung */
    uint32_t last_tx_end_ms;
    uint32_t last_rx_ms;         /* letztes empfangenes Byte */
    bool     rx_seen;
    bool     hold_active;        /* Schutzzeit nach Wiederholung */
    uint32_t hold_until_ms;

    /* Sendewarteschlange */
    bm_queue_entry_t queue[BUSMASTER_QUEUE_LEN];
    uint8_t          queue_count;
    uint32_t         queue_drops;

    /* Enumeration */
    bool             chain_active;   /* Soll-Pegel der Master-CHAIN-Leitung */
    bm_enum_phase_t  enum_phase;
    uint8_t          enum_next_addr;
    uint32_t         enum_step_ms;
    uint8_t          enum_resets_sent;
    uint8_t          enum_assign_repeat;  /* ENUM_ASSIGN fuer diese Adresse wiederholt */
    uint8_t          enum_uid_addr;       /* Verifikationslauf: naechste Adresse */
    bool             enum_svc_done;       /* Verifikationslauf: PING 250 erledigt */
    uint8_t          enum_chain_count;    /* per CHAIN bestaetigte Karten */
    uint8_t          enum_found;          /* hoechste antwortende Adresse */
    uint8_t          enum_probe_addr;     /* Nachsondierung: naechste Adresse */
    uint8_t          enum_probe_limit;    /* Nachsondierung bis einschliesslich */
    uint32_t         enum_probe_hits;     /* Bit a-1: Adresse a antwortete */
    uint8_t          enum_hint;           /* erwartete Modulzahl (z. B. aus NVS) */

    /* Abfrageplan (busmaster_service_poll) */
    uint8_t  poll_cursor;            /* naechste Statusadresse 1..count */
    uint32_t last_status_poll_ms;
    uint8_t  ver_cursor;
    uint32_t last_version_poll_ms;
    uint32_t last_service_probe_ms;
    bool     round_end_pending;      /* letzte Adresse der Runde abgefragt */
    bool     round_done;             /* Runde fertig, noch nicht gemeldet  */

    /* Warnungen und Statistik */
    uint8_t  warn;                   /* BM_WARN_* */
    uint32_t warn_seq;               /* zaehlt jede neue Warnung */
    uint32_t crc_errors;             /* CRC-Fehler auf dem Bus seit Start */
    uint32_t timeouts;               /* ausgebliebene Antworten (nach Retries) */
    uint32_t reconciles;             /* gesendete Soll/Ist-Korrekturen */

    uint32_t led_sync_ms;            /* Sendeende des letzten CMD_LED_SYNC */
} busmaster_t;

void busmaster_init(busmaster_t *bm,
                    void (*tx)(void *, const uint8_t *, size_t), void *tx_ctx);

/* Uhr setzen (NULL = aus). Siehe Kopfkommentar "Zeitbasis". */
void busmaster_set_clock(busmaster_t *bm, busmaster_clock_fn now_fn, void *ctx);

/* Diagnose-Log registrieren/abschalten (fn=NULL). Siehe busmaster_log_fn. */
void busmaster_set_log(busmaster_t *bm, busmaster_log_fn fn, void *log_ctx);

/* Ein empfangenes Byte verarbeiten (Antwortrahmen). */
void busmaster_on_rx_byte(busmaster_t *bm, uint8_t byte, uint32_t now_ms);

/* Fremden Busverkehr melden (z. B. Bytes, die waehrend eines Modul-Updates an
 * lib/moduleupdate gehen): zaehlt fuer die Busruhe wie ein empfangenes Byte. */
void busmaster_note_activity(busmaster_t *bm, uint32_t now_ms);

/* Zeitfortschritt: Timeout, Wiederholung, Enumeration, Warteschlange.
 * Direkt nach dem Einspeisen der Empfangsbytes aufrufen. */
void busmaster_tick(busmaster_t *bm, uint32_t now_ms);

/* Abfrageplan: alle BUSMASTER_POLL_INTERVAL_MS ein GET_STATUS rundlaufend
 * ueber 1..count; dazwischen GET_VERSION fuer online-Module ohne Version
 * (eigener Takt und Zeiger) und alle BUSMASTER_SERVICE_PROBE_MS ein PING an
 * Adresse 250. Sendet nur bei freiem Bus und leerer Warteschlange.
 * Rueckgabe true genau einmal je abgeschlossener Statusrunde (Antwort bzw.
 * Fehlschlag der letzten Adresse verarbeitet). */
bool busmaster_service_poll(busmaster_t *bm, uint8_t count, uint32_t now_ms);

/* true, wenn sofort gesendet werden duerfte und nichts ansteht (keine
 * Antwort, keine Enumeration, leere Warteschlange, Busruhe). */
bool busmaster_idle(const busmaster_t *bm, uint32_t now_ms);

/* Anzeige: Soll je Modul setzen und SET_ALL + GO einreihen. Ein noch nicht
 * gesendetes SET_ALL wird durch den neuen Inhalt ersetzt. */
void busmaster_show(busmaster_t *bm, const uint8_t *blaetter, uint8_t count);

/* Soll aller Module verwerfen (Betriebsart "Aus"): kein Soll/Ist-Abgleich. */
void busmaster_clear_targets(busmaster_t *bm);

/* GET_STATUS / GET_VERSION sofort senden, wenn der Bus frei ist (sonst nichts).
 * Fuer den regulaeren Betrieb busmaster_service_poll verwenden. */
void busmaster_poll_status(busmaster_t *bm, uint8_t addr, uint32_t now_ms);
void busmaster_poll_version(busmaster_t *bm, uint8_t addr, uint32_t now_ms);

/* CMD_GET_CONFIG einreihen; die Antwort fuellt mod[addr-1].cfg_*. */
void busmaster_poll_config(busmaster_t *bm, uint8_t addr, uint32_t now_ms);

/* Versionsangabe eines Moduls verwerfen (z. B. nach einem Bus-Update), damit
 * der Abfrageplan sie neu einholt; setzt auch den Fehlversuchszaehler zurueck. */
void busmaster_invalidate_version(busmaster_t *bm, uint8_t addr);

/* LED_SYNC-Broadcast einreihen (hoechstens einer wartet). led_sync_ms wird
 * beim tatsaechlichen Senden gesetzt. false nur bei voller Warteschlange. */
bool busmaster_led_sync(busmaster_t *bm, uint32_t now_ms);

/* HOME / STOP einreihen; addr 0 = Broadcast, Unicast erwartet ein ACK.
 * STOP setzt den Soll/Ist-Abgleich der betroffenen Module bis zur naechsten
 * Anzeige aus, HOME gibt ihm neue Versuche. */
void busmaster_home(busmaster_t *bm, uint8_t addr);
void busmaster_stop(busmaster_t *bm, uint8_t addr);

/* IDENTIFY einreihen (Unicast, ACK, Spezifikation 5.4). */
void busmaster_identify(busmaster_t *bm, uint8_t addr, uint8_t seconds);

/* SET_CONFIG einreihen (4 Byte, ACK, Spezifikation 5.4 / 6.3). */
void busmaster_set_config(busmaster_t *bm, uint8_t addr, uint8_t blattzahl,
                          uint8_t offset, uint8_t vorhalt, uint8_t flags);

/* Enumeration anfordern (Abschnitt 4.5.1). Beginnt bei freiem Bus in
 * busmaster_tick; eine laufende wird nicht neu gestartet. module_count und
 * online bleiben bis zum Abschluss (ENUM_DONE) auf dem alten Stand. */
void busmaster_start_enumeration(busmaster_t *bm, uint32_t now_ms);

/* Erwartete Modulzahl fuer die Nachsondierung (#22): Endet die Kette vor
 * max(hint, vorige Modulzahl), fragt der Master die Adressen dahinter per
 * PING ab; Karten hinter einer toten Karte sind nach ENUM_DONE unter ihrer
 * EEPROM-Adresse erreichbar (4.5.2) und zaehlen dann zur Modulzahl, die
 * fehlende Position bleibt offline (BM_WARN_ENUM_GAP). 0 = nur vorige Zahl. */
void busmaster_set_enum_hint(busmaster_t *bm, uint8_t expected_count);

/* true, solange eine Enumeration angefordert ist oder laeuft (einschliesslich
 * Nachsondierung und Verifikationslauf). */
bool busmaster_enum_busy(const busmaster_t *bm);

/* Aktuelle Warnungen (BM_WARN_*). UID-, Limit- und Serviceadress-Warnungen
 * spiegeln den letzten Lauf bzw. die letzte Abfrage; BM_WARN_COLLISION bleibt
 * bis busmaster_ack_warnings stehen. */
uint8_t busmaster_warnings(const busmaster_t *bm);
void    busmaster_ack_warnings(busmaster_t *bm, uint8_t mask);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_BUSMASTER_H */
