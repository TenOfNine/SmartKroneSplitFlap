/* Siehe otaverify.h. */
#include "otaverify.h"

#include <string.h>

ota_hdr_result_t otaverify_parse_header(const uint8_t *buf, size_t len,
                                        uint32_t max_img, ota_header_t *out)
{
    if (buf == NULL || out == NULL || len < OTA_HEADER_LEN) {
        return OTA_HDR_SHORT;
    }
    if (memcmp(buf, OTA_MAGIC, 4) != 0) {
        return OTA_HDR_BAD_MAGIC;
    }
    if (buf[4] != OTA_FORMAT_VER) {
        return OTA_HDR_BAD_VERSION;
    }

    const uint32_t img_len = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) |
                             ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24);
    if (img_len == 0u || (max_img != 0u && img_len > max_img)) {
        return OTA_HDR_BAD_LENGTH;
    }

    out->img_len = img_len;
    memcpy(out->img_sha256, buf + 12, OTA_SHA_LEN);
    memcpy(out->sig, buf + 44, OTA_SIG_LEN);
    return OTA_HDR_OK;
}

const char *otaverify_strerror(ota_hdr_result_t r)
{
    switch (r) {
    case OTA_HDR_OK:          return "ok";
    case OTA_HDR_SHORT:       return "Datei zu kurz";
    case OTA_HDR_BAD_MAGIC:   return "kein KRONE-Image";
    case OTA_HDR_BAD_VERSION: return "unbekannte Containerversion";
    case OTA_HDR_BAD_LENGTH:  return "unplausible Image-Laenge";
    default:                  return "unbekannter Fehler";
    }
}
