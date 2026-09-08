/* Geraeteseitige Krypto fuer das signierte Browser-OTA (ota_sign.cpp). */
#ifndef KRONE_OTA_SIGN_H
#define KRONE_OTA_SIGN_H

#include <stddef.h>
#include <stdint.h>

#include "otaverify.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Prueft die ECDSA-P-256-Signatur ueber signed_bytes[0..len) gegen den
 * einkompilierten oeffentlichen Schluessel (lib/otaverify/ota_pubkey.h). */
bool ota_sign_verify_header(const uint8_t *signed_bytes, size_t len,
                            const uint8_t sig[OTA_SIG_LEN]);

/* Streaming-SHA-256 ueber das App-Image. */
void ota_sha256_begin(void);
void ota_sha256_update(const uint8_t *data, size_t len);
void ota_sha256_finish(uint8_t out[OTA_SHA_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* KRONE_OTA_SIGN_H */
