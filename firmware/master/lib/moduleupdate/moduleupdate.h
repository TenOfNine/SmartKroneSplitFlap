/*
 * Sende-Seite der Firmware-Verteilung ueber den Bus (Master).
 *
 * Nimmt eine Warteschlange von Moduladressen entgegen und spielt ihnen der
 * Reihe nach das gebundelte App-Image ein:
 *   ENTER_BOOTLOADER -> (Reset) -> FW_BEGIN -> FW_DATA... -> FW_END -> (Reset)
 *   -> GET_VERSION zur Bestaetigung.
 *
 * Hardwareunabhaengig, gegen einen simulierten Bus host-testbar (wie busmaster).
 * Der Aufrufer stellt eine Sende-Funktion bereit, fuettert Antwortrahmen ein und
 * ruft moduleupdate_tick() mit einer Millisekunden-Uhr.
 *
 * EXPERIMENTELL -- der zugehoerige Modul-Bootloader ist am Geraet noch nicht
 * verifiziert. Siehe docs/module-bootloader.md.
 */
#ifndef KRONE_MODULEUPDATE_H
#define KRONE_MODULEUPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MU_MAX_ADDR   32u
#define MU_CHUNK      30u    /* PROTO_FW_CHUNK */

typedef enum {
    MU_RES_NONE = 0,   /* nicht in der Warteschlange */
    MU_RES_QUEUED,
    MU_RES_RUNNING,
    MU_RES_OK,
    MU_RES_FAILED,
    MU_RES_SKIPPED,    /* offline */
} mu_result_t;

typedef enum {
    MU_P_IDLE = 0,
    MU_P_ENTER,
    MU_P_WAIT_BOOT,
    MU_P_BEGIN,
    MU_P_DATA,
    MU_P_END,
    MU_P_WAIT_APP,
    MU_P_CONFIRM,
} mu_phase_t;

typedef struct {
    void  (*tx)(void *ctx, const uint8_t *frame, size_t len);
    void   *tx_ctx;

    const uint8_t *image;
    uint32_t       image_len;
    uint16_t       image_crc;      /* CRC16/MODBUS ueber image */
    uint16_t       target_ver;     /* erwartete App-Version (hi<<8|lo) zur Bestaetigung */

    uint32_t queue;               /* Bitmaske Adressen 1..MU_MAX_ADDR */
    uint32_t online;              /* vom Aufrufer gesetzt: erreichbare Adressen */
    uint8_t  cur;                 /* laufende Adresse, 0 = keine */
    mu_phase_t phase;
    uint32_t off;                 /* gesendete Bytes fuer cur */
    uint8_t  retries;
    uint32_t t_phase;             /* millis bei Phasenbeginn */
    bool     awaiting;            /* auf ACK warten */

    mu_result_t result[MU_MAX_ADDR + 1u];
    uint8_t  done_ok;
    uint8_t  done_fail;
} moduleupdate_t;

void moduleupdate_init(moduleupdate_t *mu,
                       void (*tx)(void *, const uint8_t *, size_t), void *tx_ctx,
                       const uint8_t *image, uint32_t image_len, uint16_t image_crc,
                       uint16_t target_ver);

/* Adressen zur Warteschlange hinzufuegen (Bitmaske, Bit 0 = Adresse 1).
 * online = Bitmaske der aktuell erreichbaren Adressen. */
void moduleupdate_enqueue(moduleupdate_t *mu, uint32_t addr_mask, uint32_t online_mask);

/* Antwortrahmen einfuettern (ACKs der Module). */
void moduleupdate_on_frame(moduleupdate_t *mu, uint8_t cmd, uint8_t addr,
                           const uint8_t *payload, uint8_t len, uint32_t now_ms);

/* Zeitscheibe. */
void moduleupdate_tick(moduleupdate_t *mu, uint32_t now_ms);

bool moduleupdate_busy(const moduleupdate_t *mu);

/* Fortschritt der laufenden Adresse in Prozent (0..100). */
uint8_t moduleupdate_progress(const moduleupdate_t *mu);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_MODULEUPDATE_H */
