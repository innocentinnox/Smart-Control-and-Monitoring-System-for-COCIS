/* Thin SHA-256 wrapper (PSA Crypto on ESP-IDF v6, classic mbedtls on v5) so the rest
 * of the code needs one call to hash, and never touches crypto contexts.
 *
 * Who uses it:
 *   - the user registry, to store and check PINs as hashes (never plain text),
 *   - the storage gatekeeper, to hash-chain every log line so that editing
 *     the log file afterwards can be detected (`log verify`).
 *
 * SHA-256 is a one-way hash: any input -> a fixed 32-byte "fingerprint".
 * Changing even one input bit changes the fingerprint completely, and it is
 * not practical to find an input that gives a chosen fingerprint.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define SHA256_LEN      32                     /* digest size in bytes */
#define SHA256_HEX_LEN  (SHA256_LEN * 2 + 1)   /* 64 hex chars + NUL */

/* SHA-256 over the concatenation of up to two buffers (b may be NULL).
 *
 * Hashing "a then b" in one go is the same as hashing one buffer holding
 * a followed by b, but avoids copying them together first (handy for
 * salt + PIN, or prev_hash + log line).
 *
 * @param a      first buffer to hash
 * @param a_len  length of a in bytes (0 allowed)
 * @param b      optional second buffer, appended after a; NULL = none
 * @param b_len  length of b in bytes (ignored when b is NULL)
 * @param out    receives the 32-byte binary digest
 * @return ESP_OK on success; ESP_FAIL if the crypto library failed, in which
 *         case out is filled with zeros (never left half-written).
 * Thread safety: no shared state, call from any task (not from an ISR).
 */
esp_err_t cocis_sha256(const void *a, size_t a_len, const void *b, size_t b_len,
                       uint8_t out[SHA256_LEN]);

/* Known-answer test: SHA-256("abc") must equal the FIPS 180-2 vector.
 * Run at boot; PINs and the log hash chain are only trustworthy if it passes.
 *
 * @return true if the hash engine produced the published reference digest,
 *         false otherwise (an error is logged).
 */
bool cocis_crypto_selftest(void);

/* Lower-case hex encoding of a digest into a SHA256_HEX_LEN buffer.
 *
 * Each byte becomes two characters, e.g. 0xA7 -> "a7".
 *
 * @param bytes  binary data to encode
 * @param len    number of bytes in `bytes`
 * @param out    destination, at least 2 * len + 1 chars (SHA256_HEX_LEN for
 *               a digest); always NUL-terminated
 */
void cocis_hex(const uint8_t *bytes, size_t len, char *out);
