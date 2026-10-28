/* SHA-256 helpers on top of PSA Crypto (see cocis_crypto.h).
 *
 * PSA is a "multi-part" API: setup -> update (any number of times) ->
 * finish. That is what lets cocis_sha256() hash two separate buffers as if
 * they were one, without copying them into a temporary buffer.
 */
#include "cocis_crypto.h"

#include <string.h>

#include "esp_log.h"
#include "psa/crypto.h"

static const char *TAG = "crypto";

esp_err_t cocis_sha256(const void *a, size_t a_len, const void *b, size_t b_len,
                       uint8_t out[SHA256_LEN])
{
    /* Safe to call repeatedly: it is a no-op once the library is up. */
    if (psa_crypto_init() != PSA_SUCCESS) {
        return ESP_FAIL;
    }

    /* Each step only runs if all earlier steps succeeded, so there is a
     * single error exit at the end instead of one per call. */
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    size_t out_len = 0;
    psa_status_t st = psa_hash_setup(&op, PSA_ALG_SHA_256);
    if (st == PSA_SUCCESS) {
        st = psa_hash_update(&op, a, a_len);
    }
    if (st == PSA_SUCCESS && b) {
        st = psa_hash_update(&op, b, b_len);
    }
    if (st == PSA_SUCCESS) {
        st = psa_hash_finish(&op, out, SHA256_LEN, &out_len);
    }
    if (st != PSA_SUCCESS) {
        /* abort releases whatever the half-finished operation holds */
        psa_hash_abort(&op);
        memset(out, 0, SHA256_LEN);         /* never leave garbage behind */
        ESP_LOGE(TAG, "SHA-256 failed (PSA status %d)", (int)st);
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool cocis_crypto_selftest(void)
{
    /* SHA-256("abc") from FIPS 180-2, appendix B.1 */
    static const uint8_t expected[SHA256_LEN] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    uint8_t digest[SHA256_LEN];
    /* "abc" is fed as "ab" + "c": the result must be the same as hashing it in one piece */
    bool ok = cocis_sha256("ab", 2, "c", 1, digest) == ESP_OK &&   /* also exercises the 2-part path */
              memcmp(digest, expected, SHA256_LEN) == 0;
    if (ok) {
        ESP_LOGI(TAG, "SHA-256 self-test passed");
    } else {
        ESP_LOGE(TAG, "SHA-256 self-test FAILED: PIN checks and log verify will not work");
    }
    return ok;
}

void cocis_hex(const uint8_t *bytes, size_t len, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        /* high nibble (top 4 bits) first, then low nibble (bottom 4 bits) */
        out[2 * i]     = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 0x0F];
    }
    out[2 * len] = '\0';
}
