#include "cocis_crypto.h"

#include "psa/crypto.h"

esp_err_t cocis_sha256(const void *a, size_t a_len, const void *b, size_t b_len,
                       uint8_t out[SHA256_LEN])
{
    /* Safe to call repeatedly: it is a no-op once the library is up. */
    if (psa_crypto_init() != PSA_SUCCESS) {
        return ESP_FAIL;
    }

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
        psa_hash_abort(&op);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void cocis_hex(const uint8_t *bytes, size_t len, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[2 * i]     = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 0x0F];
    }
    out[2 * len] = '\0';
}
