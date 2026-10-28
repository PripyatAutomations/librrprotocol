#ifndef RRPROTOCOL_CRYPTO_H
#define RRPROTOCOL_CRYPTO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
/* Provider-neutral protocol cryptography. Keys are opaque; bytes are stable. */
typedef struct rr_crypto_key rr_crypto_key_t;
bool rr_crypto_init(void);
rr_crypto_key_t *rr_crypto_ed25519_generate(void);
rr_crypto_key_t *rr_crypto_key_read_pem(FILE *stream);
/* Import an encrypted private key using a supplied passphrase. */
rr_crypto_key_t *rr_crypto_key_read_encrypted_pem(FILE *stream, const char *passphrase);
/* Export an AES-256-CBC encrypted PKCS#8 private key; empty password rejected. */
bool rr_crypto_key_write_encrypted_pem_stream(const rr_crypto_key_t *key,
   FILE *stream, const char *passphrase);
bool rr_crypto_key_write_pem_stream(const rr_crypto_key_t *key, FILE *stream);
rr_crypto_key_t *rr_crypto_key_load_pem(const char *path);
bool rr_crypto_key_write_pem(const rr_crypto_key_t *key, const char *path);
rr_crypto_key_t *rr_crypto_ed25519_public(const uint8_t *raw, size_t len);
bool rr_crypto_ed25519_public_bytes(const rr_crypto_key_t *key, uint8_t out[32]);
bool rr_crypto_ed25519_sign(const rr_crypto_key_t *key, const void *data, size_t len, uint8_t signature[64]);
bool rr_crypto_ed25519_verify(const rr_crypto_key_t *key, const void *data, size_t len, const uint8_t signature[64]);
bool rr_crypto_sha256(const void *data, size_t len, uint8_t digest[32]);
bool rr_crypto_random(void *data, size_t len);
void rr_crypto_key_free(rr_crypto_key_t *key);
/* Create a self-signed P-256 TLS identity only if neither file exists. */
bool rr_crypto_tls_identity_ensure(const char *cert_path, const char *key_path, const char *dns_name);
/* SPKI SHA256 of certificate public key, encoded as base64. Caller frees. */
char *rr_crypto_cert_spki_sha256(const char *cert_path);
/* Bind a catalog signing identity to a TLS SPKI for a specific domain/owner.
 * The verifier MUST separately authenticate the presented TLS certificate. */
bool rr_crypto_binding_sign(const rr_crypto_key_t *identity, const char *domain,
                            const char *owner, const char *spki_sha256_b64,
                            uint8_t signature[64]);
bool rr_crypto_binding_verify(const uint8_t public_key[32], const char *domain,
                              const char *owner, const char *spki_sha256_b64,
                              const uint8_t signature[64]);
const char *rr_crypto_provider(void);
#endif
