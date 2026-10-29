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
#include <openssl/sha.h>

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
   unsigned char digest[SHA_DIGEST_LENGTH];

   if (!SHA1((const unsigned char *)passwd, strlen(passwd), digest)) {
      return NULL;
   }
   char *hex_output = malloc(SHA_DIGEST_LENGTH * 2 + 1);

   if (!hex_output) {
      return NULL;
   }
   static const char hex[] = "0123456789abcdef";

   for (size_t i = 0 ; i < SHA_DIGEST_LENGTH ; i++) {
      hex_output[i * 2] = hex[digest[i] >> 4];
      hex_output[i * 2 + 1] = hex[digest[i] & 0x0f];
   }

   hex_output[SHA_DIGEST_LENGTH * 2] = '\0';

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
   SHA_CTX ctx;
   SHA1_Init(&ctx);
   SHA1_Update(&ctx, password, strlen(password));
   SHA1_Update(&ctx, "+", 1);
   SHA1_Update(&ctx, nonce, strlen(nonce));
   SHA1_Final(hash, &ctx);

   // Convert the raw hash to a hexadecimal string
   for (int i = 0 ; i < 20 ; i++) {
      sprintf(hex_output + (i * 2), "%02x", hash[i]);
   }

   hex_output[HTTP_HASH_LEN * 2] = '\0';  // Null-terminate the string

   return hex_output;
}
