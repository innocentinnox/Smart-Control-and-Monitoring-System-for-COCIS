/* Thin wrapper over the PSA Crypto API (mbedtls 4 in ESP-IDF v6) so the rest
 * of the code needs one call to hash, and never touches crypto contexts. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define SHA256_LEN      32
#define SHA256_HEX_LEN  (SHA256_LEN * 2 + 1)   /* 64 hex chars + NUL */

/* SHA-256 over the concatenation of up to two buffers (b may be NULL). */
esp_err_t cocis_sha256(const void *a, size_t a_len, const void *b, size_t b_len,
                       uint8_t out[SHA256_LEN]);

/* Lower-case hex encoding of a digest into a SHA256_HEX_LEN buffer. */
void cocis_hex(const uint8_t *bytes, size_t len, char *out);
