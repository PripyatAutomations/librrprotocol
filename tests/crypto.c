#include "../crypto.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void) {
   assert(rr_crypto_init());
   rr_crypto_key_t *key = rr_crypto_ed25519_generate();
   assert(key);
   unsigned char pub[32], sig[64], hash[32], rnd[32];
   assert(rr_crypto_ed25519_public_bytes(key, pub));
   /* Protected export and import must round-trip, and a wrong password must never produce a usable private key. */
   FILE *encrypted = tmpfile();
   assert(encrypted);
   assert(!rr_crypto_key_write_encrypted_pem_stream(key, encrypted, ""));
   assert(rr_crypto_key_write_encrypted_pem_stream(key, encrypted, "simple password"));
   rewind(encrypted);
   assert(!rr_crypto_key_read_encrypted_pem(encrypted, "incorrect"));
   rewind(encrypted);
   rr_crypto_key_t *imported = rr_crypto_key_read_encrypted_pem(encrypted, "simple password");
   assert(imported);
   unsigned char imported_pub[32];
   assert(rr_crypto_ed25519_public_bytes(imported, imported_pub));
   assert(memcmp(pub, imported_pub, sizeof(pub)) == 0);
   rr_crypto_key_free(imported);
   fclose(encrypted);

   rr_crypto_key_t *verify = rr_crypto_ed25519_public(pub, sizeof(pub));
   assert(verify);
   const char *message = "rustyrig crypto backend test";
   assert(rr_crypto_ed25519_sign(key, message, strlen(message), sig));
   assert(rr_crypto_ed25519_verify(verify, message, strlen(message), sig));
   assert(!rr_crypto_ed25519_verify(verify, "other", 5, sig));
   assert(rr_crypto_sha256(message, strlen(message), hash));
   assert(rr_crypto_random(rnd, sizeof(rnd)));
   assert(rr_crypto_binding_sign(key, "camp.local", "5bd92d3b-0108-4594-adc5-0cab40ef5f8c", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", sig));
   assert(rr_crypto_binding_verify(pub, "camp.local", "5bd92d3b-0108-4594-adc5-0cab40ef5f8c", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", sig));
   assert(!rr_crypto_binding_verify(pub, "other.local", "5bd92d3b-0108-4594-adc5-0cab40ef5f8c", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", sig));
   assert(!rr_crypto_binding_verify(pub, "camp.local", "5bd92d3b-0108-4594-adc5-0cab40ef5f8c", "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB=", sig));
   rr_crypto_key_free(verify);
   rr_crypto_key_free(key);
   char dir[] = "/tmp/rrcrypto-test-XXXXXX";
   assert(mkdtemp(dir));
   char cert[256], priv[256];
   snprintf(cert, sizeof(cert), "%s/cert.pem", dir);
   snprintf(priv, sizeof(priv), "%s/key.pem", dir);
   assert(rr_crypto_tls_identity_ensure(cert, priv, "test.rustyrig.local"));
   char *pin = rr_crypto_cert_spki_sha256(cert);
   assert(pin && strlen(pin) == 44);
   assert(rr_crypto_tls_identity_ensure(cert, priv, "test.rustyrig.local"));
   free(pin);
   unlink(cert);
   unlink(priv);
   rmdir(dir);
   puts("crypto provider OK");

   return 0;
}
