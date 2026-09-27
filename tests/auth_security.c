// Authentication primitives and privilege matching must reject malformed or
// insufficient inputs without granting access.
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/auth.h>

http_user_t http_users[HTTP_MAX_USERS];
time_t now;
bool dying;
bool restarting;

static void test_privileges(void) {
   assert(match_priv("admin,radio,media.*", "admin"));
   assert(match_priv("admin,radio,media.*", "radio"));
   assert(match_priv("admin,radio,media.*", "media.source"));
   assert(!match_priv("admin,radio,media.*", "mediasource"));
   assert(!match_priv("admin,radio,media.*", "media"));
   assert(!match_priv(NULL, "admin"));
   assert(!match_priv("admin", NULL));

   snprintf(http_users[3].privs, sizeof(http_users[3].privs), "%s", "admin,radio");
   assert(has_priv(3, "owner|admin"));
   assert(has_priv(3, "tx|radio"));
   assert(!has_priv(3, "owner|elmer"));
   assert(!has_priv(-1, "admin"));
   assert(!has_priv(HTTP_MAX_USERS, "admin"));
   assert(!has_priv(3, NULL));
}

static void test_wire_password(void) {
   char *wire = compute_wire_password("abc", "nonce");
   assert(wire);
   assert(strcmp(wire, "3927d6938791e4932201456ff50e5feeb99773f8") == 0);
   free(wire);
   assert(compute_wire_password(NULL, "nonce") == NULL);
   assert(compute_wire_password("abc", NULL) == NULL);
}

static void test_nonce_generation(void) {
   char nonce[HTTP_TOKEN_LEN + 1];
   int generated = auth_generate_nonce(nonce, sizeof(nonce));
   assert(generated == HTTP_TOKEN_LEN);
   assert(strlen(nonce) == HTTP_TOKEN_LEN);
   for (size_t i = 0; i < strlen(nonce); i++) {
      assert((nonce[i] >= 'A' && nonce[i] <= 'Z') ||
             (nonce[i] >= 'a' && nonce[i] <= 'z') ||
             (nonce[i] >= '0' && nonce[i] <= '9') ||
             nonce[i] == '+' || nonce[i] == '/');
   }

   char tiny[2] = { 'x', 'x' };
   assert(auth_generate_nonce(tiny, sizeof(tiny)) == 1);
   assert(tiny[1] == '\0');
   assert(auth_generate_nonce(NULL, sizeof(tiny)) == -1);
   assert(auth_generate_nonce(tiny, 1) == -1);
}

int main(void) {
   test_privileges();
   test_wire_password();
   test_nonce_generation();
   puts("PASS: authentication hashes, nonce bounds, privilege matching, and invalid inputs");
   return 0;
}
