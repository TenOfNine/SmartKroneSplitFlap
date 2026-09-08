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
 *   5       3       reserviert
 *   8       4       img_len (uint32 LE)
 *   12      32      SHA-256 des App-Images
 *   44      64      ECDSA-P-256-Signatur (r||s) ueber Byte 0..43
 *   108     ...     App-Image
 */
#ifndef KRONE_OTAVERIFY_H
#define KRONE_OTAVERIFY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OTA_MAGIC        "KRN1"
#define OTA_FORMAT_VER   1u
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
} ota_hdr_result_t;

typedef struct {
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

/* Klartext zu einem Ergebniscode (fuer Log/HTTP-Antwort). */
const char *otaverify_strerror(ota_hdr_result_t r);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_OTAVERIFY_H */
