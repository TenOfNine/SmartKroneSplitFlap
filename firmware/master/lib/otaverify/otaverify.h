/*
 * Container-Parser fuer das signierte Browser-OTA (.kota), siehe
 * tools/ota_keys.py und docs/firmware-signing.md.
 *
 * Hardwareunabhaengig und auf dem Host testbar. Die eigentliche
 * ECDSA-Pruefung (mbedTLS) liegt in src/ota_sign.cpp.
 *
 *   Offset  Laenge  Inhalt
 *   0       4       Magic "KRN1"
 *   4       1       Formatversion (=1)
 *   5       1       Ziel: 1 Zentralsteuerung, 2 Modul (0 = unbekannt)
 *   6       2       Version MAJOR, MINOR (0, 0 = unbekannt)
 *
 * Byte 5..7 waren bis Firmware 1.18 reserviert (0) und liegen im signierten
 * Teil. Aeltere Zentralsteuerungen ignorieren sie; die Formatversion bleibt
 * deshalb 1, damit auch sie neue Container annehmen (#38).
 *   8       4       img_len (uint32 LE)
 *   12      32      SHA-256 des App-Images
 *   44      64      ECDSA-P-256-Signatur (r||s) ueber Byte 0..43
 *   108     ...     App-Image
 */
#ifndef KRONE_OTAVERIFY_H
#define KRONE_OTAVERIFY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_MAGIC        "KRN1"
#define OTA_FORMAT_VER   1u

#define OTA_TARGET_UNKNOWN 0u   /* Container bis Firmware 1.18 */
#define OTA_TARGET_MASTER  1u
#define OTA_TARGET_MODULE  2u
#define OTA_HEADER_LEN   108u   /* Gesamtlaenge des Headers               */
#define OTA_SIGNED_LEN   44u    /* durch die Signatur abgedeckte Bytes    */
#define OTA_SIG_LEN      64u
#define OTA_SHA_LEN      32u

typedef enum {
    OTA_HDR_OK          = 0,
    OTA_HDR_SHORT       = -1,   /* weniger als OTA_HEADER_LEN Byte        */
    OTA_HDR_BAD_MAGIC   = -2,   /* kein KRONE-Image                       */
    OTA_HDR_BAD_VERSION = -3,   /* unbekannte Formatversion               */
    OTA_HDR_BAD_LENGTH  = -4,   /* img_len 0 oder groesser als max_img    */
    OTA_HDR_WRONG_TARGET = -5,  /* Container fuer ein anderes Geraet      */
    OTA_HDR_DOWNGRADE   = -6,   /* aelter als der laufende Stand oder
                                   Stand unbekannt (bis 1.18)             */
} ota_hdr_result_t;

typedef struct {
    uint8_t  target;         /* OTA_TARGET_* */
    uint16_t version;        /* MAJOR << 8 | MINOR, 0 = unbekannt */
    uint32_t img_len;
    uint8_t  img_sha256[OTA_SHA_LEN];
    uint8_t  sig[OTA_SIG_LEN];
} ota_header_t;

/*
 * Prueft die ersten OTA_HEADER_LEN Byte von buf. max_img = groesste
 * zulaessige Image-Laenge (freier App-Partitionsplatz). Bei OTA_HDR_OK
 * ist *out gefuellt.
 */
ota_hdr_result_t otaverify_parse_header(const uint8_t *buf, size_t len,
                                        uint32_t max_img, ota_header_t *out);

/*
 * Passt der (signaturgepruefte) Header zu diesem Geraet? Ein Container fuer
 * ein anderes Ziel wird immer abgelehnt. Eine aeltere Version oder ein
 * Container ohne Angaben (bis 1.18) nur mit allow_downgrade; dieselbe
 * Version ist erlaubt (Neuinstallation). Anti-Rollback (#38).
 */
ota_hdr_result_t otaverify_check_target(const ota_header_t *h, uint8_t target,
                                        uint16_t current_version, bool allow_downgrade);

/* Klartext zu einem Ergebniscode (fuer Log/HTTP-Antwort). */
const char *otaverify_strerror(ota_hdr_result_t r);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_OTAVERIFY_H */
