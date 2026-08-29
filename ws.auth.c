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

extern dict *cfg;
extern time_t now;
extern const char *server_name;
extern char session_token[HTTP_TOKEN_LEN + 1];	// TODO: Move into the ws_conn structure

bool ws_handle_client_auth_msg(rrconn_t *cptr, dict *d) {
   bool rv = false;

   if (!cptr || !d) {
      Log(LOG_WARN, "http.ws", "auth_msg: got msg from cptr:<%p> msg:<%p>", cptr, d);
      return true;
   }

   char *ip = cptr->user_ip;
   int port = cptr->user_port;

   const char *cmd = dict_get(d, "auth.cmd", NULL);
   const char *nonce = dict_get(d, "auth.nonce", NULL);
   const char *user = dict_get(d, "auth.user", NULL);
   time_t ts = dict_get_time_t(d, "auth.ts", now);

   // Must always send a command and username during auth
   if (!cmd || !user) {
      rv = true;
      goto cleanup;
   }

   if (cmd && strcasecmp(cmd, "challenge") == 0) {
      const char *token = dict_get(d, "auth.token", NULL);
      time_t ts = dict_get_time_t(d, "auth.ts", now);

      if (token) {
         memset(session_token, 0, HTTP_TOKEN_LEN + 1);
         snprintf(session_token, HTTP_TOKEN_LEN + 1, "%s", token);
      } else {
         Log(LOG_CRIT, "rrproto.auth", "CHALLENGE with invalid token from %s", server_name);
         goto cleanup;
      }
      const char *login_pass = get_server_property(server_name, "server.pass");
      Log(LOG_AUDIT, "ws.auth", "Got CHALLENGE %s from server %s, sending password!", nonce, server_name);
      ws_send_passwd(cptr, user, login_pass, nonce);
      event_emit_dict("logging-in", NULL, d);
   } else if (cmd && strcasecmp(cmd, "authorized") == 0) {
      event_emit_dict("authorized", NULL, d);
   }

cleanup:
   return rv;
}

bool ws_send_login(rrconn_t *cptr, const char *login_user) {
   if (!cptr || !login_user) {
      Log(LOG_DEBUG, "ws.auth", "send_login cptr:<%p> login_user:<%p> |%s|", cptr, login_user, login_user);
      return true;
   }
   Log(LOG_INFO, "rrproto.auth", "Sending initial LOGIN!");
   dict *auth_msg = dict_new();
   dict_add(auth_msg, "msg.type", "auth");
   dict_add(auth_msg, "auth.cmd", "login");
   dict_add(auth_msg, "auth.user", login_user);
   ws_send_dict(NULL, cptr, auth_msg, WEBSOCKET_OP_TEXT);
   dict_free(auth_msg);

   return false;
}

// Hashes the user stored password with the server nonce and returns it
bool ws_send_passwd(rrconn_t *cptr, const char *user, const char *passwd, const char *nonce) {
   if (!cptr || !user || !passwd || !nonce) {
      Log(LOG_CRIT, "auth", "ws_send_passwd with invalid parameters, cptr:<%p> user:<%p> passwd:<%p> nonce:<%p>", cptr, user,
         passwd, nonce);

      return true;
   }
   char *temp_pw = compute_wire_password(hash_passwd(passwd), nonce);

   if (!temp_pw) {
      Log(LOG_CRIT, "auth", "Failed to hash session password (nonce: |%s|)", nonce);
      return true;
   }
   dict *auth_msg = dict_new();
   dict_add(auth_msg, "msg.type", "auth");
   dict_add(auth_msg, "auth.cmd", "pass");
   dict_add(auth_msg, "auth.user", user);
   dict_add(auth_msg, "auth.pass", temp_pw);
   dict_add(auth_msg, "auth.token", session_token);
   ws_send_dict(NULL, cptr, auth_msg, WEBSOCKET_OP_TEXT);
   dict_free(auth_msg);
   free(temp_pw);

   return false;
}

bool ws_send_logout(rrconn_t *cptr, const char *user, const char *token) {
   if (!user || !token || !cptr) {
      Log(LOG_DEBUG, "ws.auth", "send_logout cptr:<%p> user:<%p> |%s|", cptr, user, user);
      return true;
   }

   dict *auth_msg = dict_new();
   dict_add(auth_msg, "msg.type", "auth");
   dict_add(auth_msg, "auth.cmd", "logout");
   dict_add(auth_msg, "auth.user", user);
   dict_add(auth_msg, "auth.token", token);
   ws_send_dict(NULL, cptr, auth_msg, WEBSOCKET_OP_TEXT);
   dict_free(auth_msg);

   return false;
}

bool ws_send_hello(rrconn_t *cptr) {
   if (!cptr) {
      Log(LOG_DEBUG, "ws.auth", "send_hello cptr:<%p>", cptr);
      return true;
   }
   char msgbuf[512];
   const char *codec = "mu08,mu08";
   int rate = 16000;
   dict *hello = dict_new();
   dict_add(hello, "msg.type", "hello");
   dict_add(hello, "hello.swver", VERSION);
   ws_send_dict(NULL, cptr, hello, WEBSOCKET_OP_TEXT);
   dict_free(hello);

   return false;
}
