/*
 * Empfangsseitiger Seiten-Sammler fuer das Firmware-Update ueber den Bus.
 * Nimmt CMD_FW_DATA-Bruchstuecke (Offset + bis zu PROTO_FW_CHUNK Byte) entgegen,
 * sortiert sie in einen 64-Byte-Seitenpuffer und ruft bei voller Seite (oder am
 * Ende) einen Schreib-Callback. Rechnet die CRC16/MODBUS ueber den gesamten
 * Datenstrom mit.
 *
 * Hardwareunabhaengig, auf dem Host testbar. Der Bootloader (firmware/bootloader)
 * nutzt es mit einem Callback, der die Seite ueber NVMCTRL in den Flash schreibt.
 *
 * Kein dynamischer Speicher, kein Registerzugriff.
 */
#ifndef KRONE_FWUPDATE_H
#define KRONE_FWUPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FWUPDATE_PAGE 64u   /* Flash-Seite ATtiny1616 */

typedef enum {
    FWUPDATE_OK = 0,
    FWUPDATE_ERR_ORDER   = 1,  /* Offset passt nicht zur erwarteten Position */
    FWUPDATE_ERR_RANGE   = 2,  /* Bruchstueck geht ueber die angekuendigte Laenge hinaus */
    FWUPDATE_ERR_WRITE   = 3,  /* Schreib-Callback meldete Fehler */
    FWUPDATE_ERR_LENGTH  = 4,  /* am Ende: empfangene Laenge != angekuendigt */
    FWUPDATE_ERR_CRC     = 5,  /* am Ende: CRC stimmt nicht */
} fwupdate_result_t;

/* Schreibt eine volle (oder mit 0xFF aufgefuellte) Seite an page_addr (relative
 * Byte-Adresse im App-Bereich, Vielfaches von FWUPDATE_PAGE). true = ok. */
typedef bool (*fwupdate_write_page_fn)(void *ctx, uint32_t page_addr,
                                       const uint8_t *page);

typedef struct {
    fwupdate_write_page_fn write_page;
    void    *ctx;
    uint32_t total_len;      /* aus CMD_FW_BEGIN */
    uint16_t expect_crc;     /* aus CMD_FW_BEGIN */
    uint32_t received;       /* Bytes bisher */
    uint16_t crc;            /* laufende CRC16/MODBUS */
    uint8_t  page[FWUPDATE_PAGE];
    uint32_t page_base;      /* Byte-Adresse der Seite im Puffer */
    uint8_t  page_fill;      /* gueltige Bytes ab page_base */
} fwupdate_t;

void fwupdate_begin(fwupdate_t *fu, fwupdate_write_page_fn write_page, void *ctx,
                    uint32_t total_len, uint16_t expect_crc);

/* Ein CMD_FW_DATA-Bruchstueck einarbeiten. */
fwupdate_result_t fwupdate_chunk(fwupdate_t *fu, uint32_t offset,
                                 const uint8_t *data, uint8_t len);

/* Abschluss: letzte Teilseite schreiben, Laenge + CRC pruefen. */
fwupdate_result_t fwupdate_finish(fwupdate_t *fu);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_FWUPDATE_H */
