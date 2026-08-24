// librrprotocol/rrclient.c
// rrcli helpers moved into librrprotocol and renamed to rrclient_*

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/rrclient.h>
#include <rrclient/ui.h>

extern char session_token[HTTP_TOKEN_LEN + 1];
const char *login_user = NULL;

#ifdef  USE_MONGOOSE
extern struct mg_mgr mgr;
struct mg_connection *ws_conn = NULL;

static void rrclient_ws_handler(struct mg_connection *c, int ev, void *ev_data) {
   if (ev == MG_EV_WS_MSG) {
      struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;

      if (msg && msg->data.buf) {
         char buf[HTTP_WS_MAX_MSG + 1];
         memset( buf, 0, sizeof(buf) );
         memcpy(buf, msg->data.buf, msg->data.len);
         dict *d = json2dict(buf);

         if (!d) {
            return;
         }
         char *cmd = dict_get(d, "talk.cmd", NULL);
         char *pong_ts = dict_get(d, "pong.ts", NULL);
         char *ping_ts = dict_get(d, "ping.ts", NULL);

         if (ping_ts) {
            dict_add(d, "type", "pong");
            dict_add_ulong(d, "ts", strtoul(ping_ts, NULL, 0));
            ws_send_dict(NULL, c, d, WEBSOCKET_OP_TEXT);
         } else if (pong_ts) {
            Log(LOG_CRAZY, "http.pong", "Received pong ts:%s", pong_ts);
         } else if (cmd && strcasecmp(cmd, "msg") == 0) {
            event_emit_dict("talk.msg", NULL, d);
         } else if (dict_get(d, "hello", NULL) ) {
            Log(LOG_DEBUG, "ws", "Got hello from server");
         } else if (dict_get(d, "auth.cmd", NULL) ) {
            Log(LOG_DEBUG, "ws", "Got auth message");
         }
         dict_free(d);
      }
   } else if (ev == MG_EV_WS_OPEN) {
      ws_connected = true;
      login_user = get_server_property(server_name, "server.user");

      if (login_user) {
         dict *d = dict_new();
         dict_add(d, "hello", "rrcli");
         dict_add(d, "hello.swver", VERSION);
         dict_add(d, "hello.hwver", "client");
         ws_send_dict(NULL, c, d, WEBSOCKET_OP_TEXT);
         dict_free(d);
         d = dict_new();
         dict_add(d, "auth.cmd", "login");
         dict_add(d, "auth.user", login_user);
         dict_add_ulong(d, "auth.ts", now);
         ws_send_dict(NULL, c, d, WEBSOCKET_OP_TEXT);
         dict_free(d);
      }

      dict *d = dict_new();
      char ts_s[64];
      memset( ts_s, 0, sizeof(ts_s) );
      snprintf(ts_s, sizeof(ts_s), "%lu", now);
      dict_add(d, "login.ts", ts_s);
      dict_add(d, "login.user", (char *)login_user);
      event_emit_dict("connected", NULL, d);
      dict_free(d);
   } else if (ev == MG_EV_CLOSE) {
      ws_connected = false;
      event_emit("goodbye", NULL, NULL);
   }
}
#endif // USE_MONGOOSE

bool rrclient_connect(const char *url) {
   if (!url) {
      return true;
   }
   event_emit("connecting", NULL, NULL);
#ifdef  USE_MONGOOSE
   ws_conn = mg_ws_connect(&mgr, url, rrclient_ws_handler, NULL, NULL);

   if (!ws_conn) {
      event_emit("http.error", NULL, NULL);

      return true;
   }
#endif // USE_MONGOOSE

   return false;
}

bool rrclient_send_chat(const char *data) {
   if (!data) {
      return true;
   }
   dict *d = dict_new();
   dict_add(d, "talk.cmd", "msg");
   dict_add(d, "talk.data", data);
   dict_add(d, "talk.msg_type", "pub");

#ifdef  USE_MONGOOSE

   if (!ws_conn) {
      dict_free(d);
      return true;
   }
   ws_send_dict(NULL, ws_conn, d, WEBSOCKET_OP_TEXT);
#endif // USE_MONGOOSE
   dict_free(d);

   return false;
}

bool rrclient_send(const char *json) {
   if (!json) {
      return true;
   }
#ifdef  USE_MONGOOSE

   if (!ws_conn) {
      return true;
   }
   mg_ws_send(ws_conn, json, strlen(json), WEBSOCKET_OP_TEXT);
#endif // USE_MONGOOSE

   return false;
}

bool rrclient_disconnect(void) {
#ifdef USE_MONGOOSE

   if (ws_conn) {
      ws_conn->is_closing = 1;
      ws_conn = NULL;
   }
#endif // USE_MONGOOSE
   ws_connected = false;

   return false;
}

void rrclient_poll_events(void) {
#ifdef  USE_MONGOOSE
   mg_mgr_poll(&mgr, 0);
#endif // USE_MONGOOSE
}

bool rrclient_autoconnect(void) {
   const char *server = cfg_get_exp("server.auto-connect");

   if (server) {
      char server_name[256];
      snprintf(server_name, sizeof(server_name), "%s", server);
      free( (void *)server );

      char fullkey[1024];
      snprintf(fullkey, sizeof(fullkey), "server:%s.server.url", server_name);
      const char *url = cfg_get_exp(fullkey);

      if (url) {
         rrclient_connect(url);
         free( (void *)url );
      }
   }

   return false;
}
