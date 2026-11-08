// librrprotocol/auth.hash.c: Hashing stuff
//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <openssl/evp.h>
#include <argon2.h>

int auth_generate_nonce(char *buffer, size_t length) {
   if (!buffer || length < 2) {
      return -1;
   }
   static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

   /* length is the complete buffer size, including its terminator. */
   size_t generated = length - 1;

   for (size_t i = 0 ; i < generated ; i++) {
      buffer[i] = base64_chars[arc4random_uniform(64)];
   }

   buffer[generated] = '\0';

   return (int)generated;
}

char *hash_passwd(const char *passwd) {
   if (!passwd) {
      return NULL;
   }
   unsigned char digest[20];
   unsigned int digest_len = 0;
   /* Legacy SHA-1 only for migration; EVP avoids removed low-level APIs. */
   if (EVP_Digest(passwd, strlen(passwd), digest, &digest_len, EVP_sha1(), NULL) != 1 || digest_len != sizeof(digest)) {
      return NULL;
   }
   char *hex_output = malloc(sizeof(digest) * 2 + 1);

   if (!hex_output) {
      return NULL;
   }
   static const char hex[] = "0123456789abcdef";

   for (size_t i = 0 ; i < sizeof(digest) ; i++) {
      hex_output[i * 2] = hex[digest[i] >> 4];
      hex_output[i * 2 + 1] = hex[digest[i] & 0x0f];
   }

   hex_output[sizeof(digest) * 2] = '\0';

   return hex_output;
}

//////////////////////////////////////////
// Compute wire password:
//
// How it works:
// Take password hash from the database and append "+" nonce
// Print the result as hex and compare it to what the user sent
//
// This provides protection against replays by
//
// You *must* free the result
char *compute_wire_password(const char *password, const char *nonce) {

   if (password == NULL || nonce == NULL) {
      Log(LOG_CRIT, "auth", "wtf compute_wire_password called with NULL password<%p> or nonce<%p>", password, nonce);

      return NULL;
   }
   char *hex_output = (char *)malloc(HTTP_HASH_LEN * 2 + 1);   // Allocate space

   // for hex string
   if (hex_output == NULL) {
      Log(LOG_CRIT, "auth", "oom in compute_wire_password");

      return NULL;
   }
   unsigned char hash[20];      // Store the raw SHA1 hash
   memset(hash, 0, sizeof(hash));

/* PARITY: rustyrig-www/js/webui.auth.js authenticate(). */
   EVP_MD_CTX *ctx = EVP_MD_CTX_new();
   unsigned int hash_len = 0;
   bool ok = ctx && EVP_DigestInit_ex(ctx, EVP_sha1(), NULL) == 1 &&
      EVP_DigestUpdate(ctx, password, strlen(password)) == 1 &&
      EVP_DigestUpdate(ctx, "+", 1) == 1 &&
      EVP_DigestUpdate(ctx, nonce, strlen(nonce)) == 1 &&
      EVP_DigestFinal_ex(ctx, hash, &hash_len) == 1 && hash_len == sizeof(hash);
   EVP_MD_CTX_free(ctx);
   if (!ok) {
      explicit_bzero(hash, sizeof(hash));
      free(hex_output);
      return NULL;
   }

   // Convert the raw hash to a hexadecimal string
   for (int i = 0 ; i < 20 ; i++) {
      sprintf(hex_output + (i * 2), "%02x", hash[i]);
   }

   hex_output[HTTP_HASH_LEN * 2] = '\0';  // Null-terminate the string

   return hex_output;
}

/* Encoded Argon2id v=19 password hashes; fresh random salt per account.
 * The 19 MiB / 2 iteration / 1 lane parameters fit constrained servers.
 * Encoded parameters remain part of the hash for future migration.
 */
char *auth_password_hash_argon2id(const char *password) {
   if (!password || !*password) {
      return NULL;
   }
   unsigned char salt[16];
   char encoded[256];
   arc4random_buf(salt, sizeof(salt));
   int rc = argon2id_hash_encoded(2, 19456, 1, password, strlen(password),
      salt, sizeof(salt), 32, encoded, sizeof(encoded));
   explicit_bzero(salt, sizeof(salt));
   if (rc != ARGON2_OK) {
      explicit_bzero(encoded, sizeof(encoded));
      return NULL;
   }
   char *stored = strdup(encoded);
   explicit_bzero(encoded, sizeof(encoded));
   return stored;
}

bool auth_password_verify_argon2id(const char *encoded, const char *password) {
   if (!encoded || !password || strncmp(encoded, "$argon2id$v=19$", 14) ||
       strlen(encoded) >= 256) {
      return false;
   }
   return argon2id_verify(encoded, password, strlen(password)) == ARGON2_OK;
}

bool auth_password_is_argon2id(const char *encoded) {
   return encoded && strncmp(encoded, "$argon2id$v=19$", 14) == 0;
}
