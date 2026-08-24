//
// rrclient/ws.auth.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrclient/connman.h>
#include <rrclient/userlist.h>

extern dict *cfg;                // config.c
extern time_t now;
extern const char *server_name;                          // Remove ASAP

// XXX: This needs moved into the ws_conn
extern char session_token[HTTP_TOKEN_LEN + 1];

#if     defined(USE_MONGOOSE)
bool ws_handle_client_auth_msg(struct mg_connection *c, dict *d) {
   bool rv = false;

   if (!c || !d) {
      Log(LOG_WARN, "http.ws", "auth_msg: got msg mg_conn:<%p> msg:<%p>", c, d);

      return true;
   }
   char ip[INET6_ADDRSTRLEN];
   int port = c->rem.port;

   if (c->rem.is_ip6) {
      inet_ntop( AF_INET6, c->rem.addr.ip6, ip, sizeof(ip) );
   } else {
      inet_ntop( AF_INET, &c->rem.addr.ip4, ip, sizeof(ip) );
   }
   char *cmd = dict_get(d, "auth.cmd", NULL);
   char *nonce = dict_get(d, "auth.nonce", NULL);
   char *user = dict_get(d, "auth.user", NULL);
   time_t ts = dict_get_time_t(d, "auth.ts", now);

   // Must always send a command and username during auth
   if (!cmd || !user) {
      rv = true;
      goto cleanup;
   }

   if (cmd && strcasecmp(cmd, "challenge") == 0) {
      char *token = dict_get(d, "auth.token", NULL);
      time_t ts = dict_get_time_t(d, "auth.ts", now);

      if (token) {
         memset(session_token, 0, HTTP_TOKEN_LEN + 1);
         snprintf(session_token, HTTP_TOKEN_LEN + 1, "%s", token);
      } else {
         Log(LOG_CRIT, "librrprotocol", "CHALLENGE with invalid token from %s", server_name);
         goto cleanup;
      }
      const char *login_pass = get_server_property(server_name, "server.pass");
      Log(LOG_INFO, "ws.auth", "Got CHALLENGE %s from server %s, sending password!", nonce, server_name);
      ws_send_passwd(c, user, login_pass, nonce);
      event_emit_dict("logging-in", NULL, d);
   } else if (cmd && strcasecmp(cmd, "authorized") == 0) {
      event_emit_dict("authorized", NULL, d);
   }

cleanup:
   return rv;
}

bool ws_send_login(struct mg_connection *c, const char *login_user) {
   if (!c || !login_user) {
      Log(LOG_DEBUG, "ws.auth", "send_login c:<%p> login_user:<%p> |%s|", c, login_user, login_user);

      return true;
   }
   Log(LOG_INFO, "librrprotocol", "Sending initial LOGIN!");
   dict *d = dict_new();
   dict_add(d, "auth.cmd", "login");
   dict_add(d, "auth.user", login_user);
   ws_send_dict(NULL, c, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

// Hashes the user stored password with the server nonce and returns it
bool ws_send_passwd(struct mg_connection *c, const char *user, const char *passwd, const char *nonce) {
   if (!c || !user || !passwd || !nonce) {
      Log(LOG_CRIT, "auth", "ws_send_passwd with invalid parameters, c:<%p> user:<%p> passwd:<%p> nonce:<%p>", c, user,
         passwd, nonce);

      return true;
   }
   char *temp_pw = compute_wire_password(hash_passwd(passwd), nonce);

   if (!temp_pw) {
      Log(LOG_CRIT, "auth", "Failed to hash session password (nonce: |%s|)", nonce);
      return true;
   }
   dict *d = dict_new();
   dict_add(d, "auth.cmd", "pass");
   dict_add(d, "auth.user", user);
   dict_add(d, "auth.pass", temp_pw);
   dict_add(d, "auth.token", session_token);
   ws_send_dict(NULL, c, d, WEBSOCKET_OP_TEXT);
   dict_free(d);
   free(temp_pw);

   return false;
}

bool ws_send_logout(struct mg_connection *c, const char *user, const char *token) {
   if (!user || !token || !c) {
      Log(LOG_DEBUG, "ws.auth", "send_logout c:<%p> user:<%p> |%s|", c, user, user);

      return true;
   }

   dict *d = dict_new();
   dict_add(d, "auth.cmd", "logout");
   dict_add(d, "auth.user", user);
   dict_add(d, "auth.token", token);
   ws_send_dict(NULL, c, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

bool ws_send_hello(struct mg_connection *c) {
   if (!c) {
      Log(LOG_DEBUG, "ws.auth", "send_hello c:<%p>", c);

      return true;
   }
   char msgbuf[512];
   const char *codec = "mulaw";
   int rate = 16000;
   dict *d = dict_new();
   dict_add(d, "hello", VERSION);
   ws_send_dict(NULL, c, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}
#endif
