//
// srv.auth.c: Server authentication stuff
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <arpa/inet.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

// This defines a hard-coded fallback path for httpd root, if not set in config
#ifdef	HOST_POSIX
#ifndef	INSTALL_PREFIX
#define	WWW_ROOT_FALLBACK "./www"
#define	WWW_404_FALLBACK "./www/404.html"
#endif // !INSTALL_PREFIX
#else	// HOST_POSIX
#define	WWW_ROOT_FALLBACK "fs:www/"
#define	WWW_404_FALLBACK "fs:www/404.html"
#endif // HOST_POSIX.else

extern bool dying;
extern time_t now;
extern char session_token[HTTP_TOKEN_LEN + 1];
extern http_user_t http_users[HTTP_MAX_USERS];

static int generate_random_guest_id(int digits) {
   if (digits < 4) {
      return -1;
   }
   int num = 0, prev_digit = -1;

try_again:
   for (int i = 0 ; i < digits ; i++) {
      int digit;
      do{
         digit = rand() % 10;
      } while (digit == prev_digit);   // Ensure no consecutive repeats

      num = num * 10 + digit;
      prev_digit = digit;
   }

   rrconn_t *cptr = http_client_list;
   while (cptr) {
      // if we match an existing number, start over
      if (cptr->guest_id == num) {
         goto try_again;
      }
      cptr = cptr->next;
   }
   return num;
}

// find by login challenge
static rrconn_t *http_find_client_by_nonce(const char *nonce) {
   if (!nonce) {
      return NULL;
   }
   rrconn_t *cptr = http_client_list;
   int i = 0;

   if (nonce == NULL) {
      return NULL;
   }
   while (cptr) {
      if (cptr->nonce[0] == '\0') {
         continue;
      }

      if (memcmp( cptr->nonce, nonce, strlen(cptr->nonce) ) == 0) {
         Log(LOG_CRAZY, "http.core", "hfcbn returning index [%i] for nonce |%s|", cptr->nonce);
         return cptr;
      }
      i++;
      cptr = cptr->next;
   }
   Log(LOG_CRAZY, "http.core", "hfcbn |%s| no matches!", nonce);

   return NULL;
}

bool match_priv(const char *user_privs, const char *priv) {
   Log(LOG_CRAZY, "auth", "match_priv(): comparing |%s| to |%s|", user_privs, priv);

   if (user_privs == NULL || priv == NULL) {
      return false;
   }
   const char *start = user_privs;
   size_t privlen = strlen(priv);

   while (start && *start) {
      const char *end = strchr(start, ',');
      size_t len = end ? (size_t)(end - start) : strlen(start);

      char token[64];

      if (len >= sizeof(token) ) {
         len = sizeof(token) - 1;
      }
      memcpy(token, start, len);
      token[len] = '\0';

      Log(LOG_CRAZY, "auth", "token=|%s|", token);

      if (strcmp(token, priv) == 0) {
         Log(LOG_CRAZY, "auth", " ! exact match |%s|", token);
         return true;
      }

      if (len >= 2 && token[len - 2] == '.' && token[len - 1] == '*') {
         token[len - 2] = '\0';   // strip .*

         if (strncmp( priv, token, strlen(token) ) == 0 && priv[strlen(token)] == '.') {
            Log(LOG_CRAZY, "auth", " ! wildcard match |%s|", token);
            return true;
         }
      }
      start = end ? end + 1 : NULL;
   }
   return false;
}

bool has_priv(int uid, const char *priv) {
   if (priv == NULL || uid < 0 || (uid > HTTP_MAX_USERS - 1) ) {
      return false;
   }
   const char *p = priv;
   while (p && *p) {
      const char *sep = strchr(p, '|');
      size_t len = sep ? (size_t)(sep - p) : strlen(p);

      char tmp[64];   // adjust size as needed

      if (len >= sizeof(tmp) ) {
         len = sizeof(tmp) - 1;
      }
      memcpy(tmp, p, len);
      tmp[len] = '\0';

      if (http_users[uid].privs[0] == '\0') {
         return false;
      }

      if (match_priv(http_users[uid].privs, tmp) ) {
         return true;
      }
      p = sep ? sep + 1 : NULL;
   }
   return false;
}

///////////////////////////////////////
bool ws_handle_auth_msg(rrconn_t *cptr, dict *d) {
   bool rv = false;

   if (!cptr || !d) {
      Log(LOG_WARN, "http.ws", "auth_msg: got cptr:<%p> d:<%p>", cptr, d);
      return true;
   }
   const char *cmd = dict_get(d, "auth.cmd", NULL);
   const char *pass = dict_get(d, "auth.pass", NULL);
   const char *token = dict_get(d, "auth.token", NULL);
   const char *user = dict_get(d, "auth.user", NULL);
   char *temp_pw = NULL;

   // Must always send a command and username during auth
   if (!cmd || (!user && !token) ) {
      return true;
   }

   if (strcasecmp(cmd, "login") == 0) {
      char resp_buf[HTTP_WS_MAX_MSG + 1];
      char *ip = cptr->user_ip;
      int port = cptr->user_port;

      Log(LOG_AUDIT, "auth", "Login request from user %s on cptr:<%p> from %s:%d", user, cptr, ip, port);

      // search for user
      for (int i = 0 ; i < HTTP_MAX_USERS ; i++) {
         if (strcasecmp(http_users[i].name, user) == 0) {
            cptr->user = &http_users[i];
            break;
         }
      }

      // handle disabled accounts
      if (cptr->user == NULL) {
         Log(LOG_AUDIT, "auth.users", "No such account %s", user);
         ws_kick_client(cptr, "Invalid account/password");
         return true;
      }

      if (cptr->user->enabled == false) {
         Log(LOG_AUDIT, "auth.users", "User account %s is disabled", user);
         ws_kick_client(cptr, "Account disabled");
         return true;
      }

      int curr_clients = http_count_clients();
      if (curr_clients > HTTP_MAX_SESSIONS) {
         Log(LOG_AUDIT, "auth.users", "Server is full! %d clients exceeds max %d", curr_clients, HTTP_MAX_SESSIONS);
         // kick the user
         ws_kick_client(cptr, "Server full! Try again later.");
         return true;
      }

      if (cptr->user) {
         if (cptr->user->clones + 1 > cptr->user->max_clones) {
            Log(LOG_AUDIT, "auth.users", "User clone limit reached for %s: %d clones exceeds max %d", cptr->user->name,
               cptr->user->clones, cptr->user->max_clones);
            // Kick the client
            ws_kick_client(cptr, "Too many clones");
            return true;
         }
      } else {
         Log(LOG_CRIT, "auth.users", "login request has no cptr->user for cptr:<%p>?!", cptr);
      }
      dict *auth_msg = dict_new();
      dict_add(auth_msg, "msg.type", "auth");
      dict_add(auth_msg, "auth.cmd", "challenge");
      dict_add(auth_msg, "auth.nonce", cptr->nonce);
      dict_add(auth_msg, "auth.user", user);
      dict_add(auth_msg, "auth.token", cptr->token);
      Log(LOG_CRAZY, "auth", "Sending login challenge |%s| to cptr <%p>, token |%s|", cptr->nonce, cptr, cptr->token);
      ws_send_dict(NULL, cptr, auth_msg, WEBSOCKET_OP_TEXT);
      dict_free(auth_msg);
   } else if (strcasecmp(cmd, "logout") == 0 || strcasecmp(cmd, "quit") == 0) {
      Log(LOG_DEBUG, "auth", "Logout request from %s cptr:<%p>",
         (cptr && cptr->chatname[0] != '\0' ? cptr->chatname : ""), cptr);
      ws_kick_client(cptr, "Logged out. 73!");
   } else if (strcasecmp(cmd, "pass") == 0) {
      bool guest = false;

      if (pass == NULL || token == NULL) {
         Log(LOG_DEBUG, "auth", "auth pass command without password <%p> / token <%p>", pass, token);
         ws_kick_client_by_c(cptr->conn, "auth.pass message incomplete/invalid. Goodbye");
         return true;
      }

      char *ip = cptr->user_ip;
      int port = cptr->user_port;

      if (cptr->user == NULL) {
         Log(LOG_WARN, "auth", "cptr-> user == NULL handling conn from ip %s:%d, Kicking!", ip, port);
         ws_kick_client(cptr, "Invalid login/password");
         return true;
      }

      int login_uid = cptr->user->uid;
      if (login_uid < 0 || login_uid > HTTP_MAX_USERS) {
         Log(LOG_WARN, "auth", "Invalid uid for username |%s| from IP %s:%d", cptr->chatname, ip, port);
         ws_kick_client(cptr, "Invalid login/passowrd");
         return true;
      }

      http_user_t *up = &http_users[login_uid];
      if (up == NULL) {
         Log(LOG_WARN, "auth", "Uid %d returned NULL http_user_t", login_uid);
         return true;
      }

      // Deal with double-hashed (reply-protected) responses
      char *nonce = cptr->nonce;
      if (nonce == NULL) {
         Log(LOG_WARN, "auth", "No nonce for user %d", login_uid);
         return true;
      }

      temp_pw = compute_wire_password(up->pass, nonce);
      if (temp_pw == NULL) {
         Log(LOG_WARN, "auth", "Got NULL return from compute_wire_password for cptr:<%p>, kicking!", cptr);
         return true;
      }
      Log(LOG_CRAZY, "auth", "Saved: |%s|, hashed (server): |%s|, received: |%s|", up->pass, temp_pw, pass);

      if (strcmp(temp_pw, pass) == 0) {
         // special handling for guests; we generate a random suffix
         // force rewriting if they use any nick starting with Guest.
         if (strncasecmp(up->name, "guest", 5) == 0) {
            cptr->guest_id = generate_random_guest_id(4);
            memset( cptr->chatname, 0, sizeof(cptr->chatname) );
            snprintf(cptr->chatname, sizeof(cptr->chatname), "GUEST%04d", cptr->guest_id);
            guest = true;
         } else {
            prepare_msg(cptr->chatname, sizeof(cptr->chatname), "%s", up->name);
         }
         cptr->authenticated = true;
         cptr->user->clones++;

         Log(LOG_AUDIT, "auth", "Verified credentials for %s", up->name);


         // Store some timestamps such as when user joined & session will
         // forcibly expire
         cptr->session_start = now;
         cptr->last_heard = now;

         // XXX: This might be a bad idea
//         cptr->session_expiry = now + HTTP_SESSION_LIFETIME;
         ////////////////////
         // Set user flags //
         ////////////////////
         if (has_priv(cptr->user->uid, "owner|syslog") ) {
            client_set_flag(cptr, FLAG_SYSLOG);
         }

         if (has_priv(cptr->user->uid, "admin|owner") ) {
            client_set_flag(cptr, FLAG_STAFF);
         }

         if (has_priv(cptr->user->uid, "tx") ) {
            client_set_flag(cptr, FLAG_CAN_TX);
         }

         // client cannot transmit unless a user with elmer flag is logged in
         if (has_priv(cptr->user->uid, "noob") ) {
            client_set_flag(cptr, FLAG_NOOB);
         }

         // client is an elmer and can allow noobs to control rig
         if (has_priv(cptr->user->uid, "elmer") ) {
            client_set_flag(cptr, FLAG_ELMER);
         }
         // Send a ping to the user and expect them to reply within
         // HTTP_PING_TIMEOUT seconds
         cptr->last_heard = now;

         // Send last message (AUTHORIZED) of the login sequence to let client
         // know they are logged in
         dict *auth_msg = dict_new();
         dict_add(auth_msg, "msg.type", "auth");
         dict_add(auth_msg, "auth.cmd", "authorized");
         dict_add(auth_msg, "auth.privs", cptr->user->privs);
         dict_add(auth_msg, "auth.token", token);
         dict_add(auth_msg, "auth.user", cptr->chatname);
         dict_add_ulong(auth_msg, "auth.ts", now);
         ws_send_dict(NULL, cptr, auth_msg, WEBSOCKET_OP_TEXT);
         dict_free(auth_msg);
         auth_msg = NULL;

         // send a ping, XXX: this might be a duplicate, confirm?
         ws_send_ping(cptr);

         Log(LOG_AUDIT, "auth", "User %s on cptr <%p> logged in from IP %s:%d (clone #%d/%d) with privs: %s",
            cptr->chatname, cptr, cptr->user_ip, cptr->user_port, cptr->user->clones, cptr->user->max_clones, cptr->user->privs);

         // Send our capabilities
         const char *my_codecs = cfg_get_exp("codecs.allowed");
         const char *capab_msg = media_capab_prepare(my_codecs);
         free( (void *)my_codecs );

         if (capab_msg) {
            mg_ws_send(cptr->conn, capab_msg, strlen(capab_msg), WEBSOCKET_OP_TEXT);
            free( (char *)capab_msg );
         } else {
            Log(LOG_CRIT, "ws.media", ">> No codecs negotiated");
         }
         /////////////////////
         // XXX: We should move this out to it's own function like
         // join_channel(cptr, "&localrig");
         // blorp out a join to all chat users
         char scratch[32];
         memset( scratch, 0, sizeof(scratch) );
         snprintf(scratch, sizeof(scratch), "%lu", (unsigned long)now);
         dict *talk_msg = dict_new();
         dict_add(talk_msg, "msg.type", "talk");
         dict_add(talk_msg, "msg.ts", scratch);
         dict_add(talk_msg, "talk.cmd", "join");
         dict_add(talk_msg, "talk.ip", ip);
         dict_add(talk_msg, "talk.muted", (cptr->user->is_muted ? "true" : "false") );
         dict_add(talk_msg, "talk.privs", cptr->user->privs);
         dict_add(talk_msg, "talk.target", "&localrig");
         dict_add(talk_msg, "talk.user", cptr->chatname);
         memset( scratch, 0, sizeof(scratch) );
         snprintf(scratch, sizeof(scratch), "%d", cptr->user->clones);
         dict_add(talk_msg, "talk.clones", scratch);
         ws_broadcast_dict(NULL, talk_msg, WEBSOCKET_OP_TEXT);
         ws_send_users(NULL);
         event_emit_dict("send-chat-replay", cptr, talk_msg);
         dict_free(talk_msg);
      } else {
         Log(LOG_AUDIT, "auth", "User %s on cptr <%p> from IP %s:%d gave wrong password. Kicking!", cptr->user, cptr, ip, port);
         ws_kick_client(cptr, "Invalid login/password");
      }

      // AUDIT: Sanitize buffers containing sensitive data before freeing
      explicit_bzero( temp_pw, sizeof(temp_pw) );
      free(temp_pw);
   }
cleanup:
   return rv;
}
