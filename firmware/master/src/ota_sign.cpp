/*
 * ECDSA-P-256-Pruefung fuer das signierte Browser-OTA.
 * Nur fuer das Zielboard (mbedTLS aus dem ESP-IDF). Der Container-Parser
 * und die Konstanten stehen hardwareunabhaengig in lib/otaverify.
 *
 * Ablauf im Upload-Handler (src/main.cpp):
 *   1. Header (108 B) puffern, otaverify_parse_header()
 *   2. ota_sign_verify_header(header, sig)  -- Signatur ueber Byte 0..43
 *   3. Image streamend an Update.write() + ota_sha256_*()
 *   4. Am Ende Streaming-Hash gegen header.img_sha256 vergleichen
 */
#include <string.h>

#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/bignum.h"
#include "mbedtls/sha256.h"

#include "ota_sign.h"

extern "C" {
#include "ota_pubkey.h"
}

bool ota_sign_verify_header(const uint8_t *signed_bytes, size_t len,
                            const uint8_t sig[OTA_SIG_LEN])
{
    uint8_t hash[32];
    {
        mbedtls_sha256_context c;
        mbedtls_sha256_init(&c);
        mbedtls_sha256_starts(&c, 0);
        mbedtls_sha256_update(&c, signed_bytes, len);
        mbedtls_sha256_finish(&c, hash);
        mbedtls_sha256_free(&c);
    }

    mbedtls_ecp_group grp;
    mbedtls_ecp_point Q;
    mbedtls_mpi r, s;
    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&Q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    bool ok = false;
    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_ecp_point_read_binary(&grp, &Q, OTA_PUBKEY, sizeof(OTA_PUBKEY)) == 0 &&
        mbedtls_mpi_read_binary(&r, sig, 32) == 0 &&
        mbedtls_mpi_read_binary(&s, sig + 32, 32) == 0) {
        ok = mbedtls_ecdsa_verify(&grp, hash, sizeof(hash), &Q, &r, &s) == 0;
    }

    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_ecp_point_free(&Q);
    mbedtls_ecp_group_free(&grp);
    return ok;
}

/* --- Streaming-SHA-256 fuer das Image ------------------------------- */
static mbedtls_sha256_context g_sha;

void ota_sha256_begin(void)
{
    mbedtls_sha256_init(&g_sha);
    mbedtls_sha256_starts(&g_sha, 0);
}

void ota_sha256_update(const uint8_t *data, size_t len)
{
    mbedtls_sha256_update(&g_sha, data, len);
}

void ota_sha256_finish(uint8_t out[OTA_SHA_LEN])
{
    mbedtls_sha256_finish(&g_sha, out);
    mbedtls_sha256_free(&g_sha);
}
