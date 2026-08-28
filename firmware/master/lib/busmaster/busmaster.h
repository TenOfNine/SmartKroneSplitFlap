/*
 * Master-Seite des Busprotokolls, siehe docs/spezifikation.md 4.5, 5.
 *
 * Hardwareunabhaengig: der Aufrufer stellt eine Sende-Funktion bereit und
 * fuettert empfangene Bytes ein. Die CHAIN-Leitung wird ueber ein Flag
 * angefordert. Auf dem Host gegen einen simulierten Bus testbar.
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
#define BUSMASTER_TIMEOUT_MS  5u    /* Spezifikation 5.6 */
#define BUSMASTER_RETRIES     2u
#define BUSMASTER_ENUM_STEP_MS 8u   /* Wartezeit auf das ENUM_ASSIGN-ACK */

typedef struct {
    bool     online;
    uint8_t  ist_blatt;
    uint8_t  ziel_blatt;
    uint8_t  zustand;    /* 0 Idle, 1 Homing, 2 Moving, 3 Fehler */
    uint8_t  fehler;
    uint8_t  blattzahl;
    uint16_t korrektur;
    uint8_t  fw_version;
    uint8_t  miss_count;
} bm_module_t;

typedef enum {
    BM_ENUM_IDLE,
    BM_ENUM_RESET_SENT,
    BM_ENUM_ASSIGNING,
    BM_ENUM_DONE,
} bm_enum_phase_t;

typedef struct {
    void (*tx)(void *ctx, const uint8_t *data, size_t len);
    void  *tx_ctx;

    uint8_t     module_count;
    bm_module_t mod[BUSMASTER_MAX_MODULES];

    proto_parser_t parser;

    uint8_t  pending_cmd;
    uint8_t  pending_addr;
    bool     awaiting;
    uint32_t sent_ms;
    uint8_t  retries;

    bool             chain_active;   /* Soll-Pegel der Master-CHAIN-Leitung */
    bm_enum_phase_t  enum_phase;
    uint8_t          enum_next_addr;
    uint32_t         enum_step_ms;
} busmaster_t;

void busmaster_init(busmaster_t *bm,
                    void (*tx)(void *, const uint8_t *, size_t), void *tx_ctx);

/* Ein empfangenes Byte verarbeiten (Antwortrahmen). */
void busmaster_on_rx_byte(busmaster_t *bm, uint8_t byte, uint32_t now_ms);

/* Zeitfortschritt: Antwort-Timeout, Wiederholungen, Enumerationsschritte. */
void busmaster_tick(busmaster_t *bm, uint32_t now_ms);

/* Anzeige aktualisieren: SET_ALL mit den Zielblaettern, danach GO. */
void busmaster_show(busmaster_t *bm, const uint8_t *blaetter, uint8_t count);

/* GET_STATUS an ein Modul; die Antwort aktualisiert mod[addr-1]. */
void busmaster_poll_status(busmaster_t *bm, uint8_t addr, uint32_t now_ms);

/* HOME / STOP; addr 0 = Broadcast. */
void busmaster_home(busmaster_t *bm, uint8_t addr);
void busmaster_stop(busmaster_t *bm, uint8_t addr);

/* SET_CONFIG an ein Modul (4 Byte, Spezifikation 5.4 / 6.3). */
void busmaster_set_config(busmaster_t *bm, uint8_t addr, uint8_t blattzahl,
                          uint8_t offset, uint8_t vorhalt, uint8_t flags);

/* Enumeration starten (Abschnitt 4.5.1). Der Ablauf laeuft ueber
 * busmaster_tick und busmaster_on_rx_byte weiter. */
void busmaster_start_enumeration(busmaster_t *bm, uint32_t now_ms);

/* true, solange eine Enumeration laeuft. */
bool busmaster_enum_busy(const busmaster_t *bm);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_BUSMASTER_H */
