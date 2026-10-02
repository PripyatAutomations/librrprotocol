// Authentication primitives and privilege matching must reject malformed or
// insufficient inputs without granting access.
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

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

static void test_wire_password_full_inputs(void) {
   const char *stored_hash = "a9993e364706816aba3e25717850c26c9cd0d89d";
   char *hashed = hash_passwd("abc");
   assert(hashed && strcmp(hashed, stored_hash) == 0);

   char *wire = compute_wire_password(hashed, "nonce");
   char *other = compute_wire_password(hashed, "noncf");
   assert(wire && other);
   assert(strcmp(wire, "7c101de03d12be5a7618daf3f9041d4d9e9df534") == 0);
   assert(strcmp(other, "ca9e972d8e5dde4c7d4628a94e010cd4a84c7cea") == 0);
   assert(strcmp(wire, other) != 0);
   free(wire);
   free(other);

   // Nonces differing only after the old combined-buffer limit must differ.
   char nonce[65];
   memset(nonce, 'A', sizeof(nonce) - 1);
   nonce[sizeof(nonce) - 1] = '\0';
   wire = compute_wire_password(hashed, nonce);
   nonce[sizeof(nonce) - 2] = 'B';
   other = compute_wire_password(hashed, nonce);
   assert(wire && other);
   assert(strcmp(wire, "27bed1cf8ecdf3c7e9c19db7bedad68e99ffdf00") == 0);
   assert(strcmp(other, "9ced84b14d2064bcc7d88c85ff05c3c3018facd5") == 0);
   assert(strcmp(wire, other) != 0);
   free(wire);
   free(other);
   free(hashed);
}

static void test_media_source_authorization(void) {
   http_user_t *user = &http_users[4];
   user->uid = 4;
   snprintf(user->privs, sizeof(user->privs), "%s", "rx");

   rrconn_t source = {0};
   source.authenticated = true;
   source.user = user;
   client_set_flag(&source, FLAG_VIDEO_SOURCE);
   assert(!media_source_authorized(&source));

   snprintf(user->privs, sizeof(user->privs), "%s", "rx,video-src");
   assert(media_source_authorized(&source));
   client_clear_flag(&source, FLAG_VIDEO_SOURCE);
   assert(!media_source_authorized(&source));

   client_set_flag(&source, FLAG_MEDIA_SOURCE);
   assert(media_source_authorized(&source));
   source.authenticated = false;
   assert(!media_source_authorized(&source));
}

static void test_http_client_owned_resources(void) {
   rrconn_t client = {0};
   const char unterminated_ua[] = { 'R', 'u', 's', 't', 'y' };
   assert(http_client_set_user_agent(&client, unterminated_ua,
      sizeof(unterminated_ua)));
   assert(strcmp(client.user_agent, "Rusty") == 0);
   client.cli_version = strdup("audit-client");
   assert(client.cli_version);

   http_client_free_resources(&client);
   assert(client.user_agent == NULL);
   assert(client.cli_version == NULL);
   http_client_free_resources(&client);
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
   test_wire_password_full_inputs();
   test_media_source_authorization();
   test_http_client_owned_resources();
   test_nonce_generation();
   puts("PASS: authentication hashes, nonce bounds, privilege matching, and invalid inputs");
   return 0;
}
