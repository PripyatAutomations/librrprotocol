//
// rrgtk/cli.main.c: Client stuff
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
#include <limits.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

extern const char *get_server_property(const char *server, const char *prop);
extern time_t now;
extern int ws_connected;
const char *tls_ca_path = NULL;
bool cfg_http_debug_crazy = false;
const char *server_name = NULL;
extern bool cfg_show_pings;
#ifdef	USE_MONGOOSE
struct mg_mgr mgr;
struct mg_str tls_ca_path_str;
#endif	// USE_MONGOOSE

// At startup, we try to find the distribution's TLS certificate authority trust store
const char *default_tls_ca_paths[] = {
   "/etc/ssl/certs/ca-certificates.crt",
   "/etc/pki/tls/certs/ca-bundle.crt",
   "/etc/ssl/cert.pem"
};

//////////////////////
// Websocket router //
//////////////////////
extern bool ws_handle_alert_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_client_auth_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_error_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_hello_msg(rrconn_t *cptr, dict *d);
//extern bool ws_handle_media_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_notice_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_ping_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_rigctl_cli_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_syslog_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_talk_msg(rrconn_t *cptr, dict *d);

struct ws_msg_routes {
   const char *type;             // auth|ping|talk|cat|alert|error|hello etc
   bool auth_reqd;               // Is this only for authenticated users?
   bool (*cb)(/*rrconn_t *cptr, dict *d*/);
};

struct ws_msg_routes ws_routes_cli[] = {
   { .type = "alert",  .cb = ws_handle_alert_msg },
   { .type = "auth",   .cb = ws_handle_client_auth_msg },
   { .type = "cat",    .cb = ws_handle_rigctl_cli_msg },
   { .type = "error",  .cb = ws_handle_error_msg },
   { .type = "hello",  .cb = ws_handle_hello_msg },
//   { .type = "media", .cb = ws_handle_media_msg },
   { .type = "notice", .cb = ws_handle_notice_msg },
   { .type = "ping",   .cb = ws_handle_ping_msg },
   { .type = "syslog", .cb = ws_handle_syslog_msg },
   { .type = "talk",   .cb = ws_handle_talk_msg },
   { .type = NULL,     .cb = NULL }
};

bool ws_handle_hello_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_DEBUG, "ws", "hello: cptr:<%p> d:<%p>", cptr, d);

      return true;
   }
   const char *h_swver = dict_get(d, "hello.swver", NULL);
   const char *h_hwver = dict_get(d, "hello.hwver", NULL);

   if (h_swver && h_hwver) {
      Log(LOG_INFO, "ws.auth", "*** server is running %s on %s ***", h_swver, h_hwver);
   } else {
      const char *jp = dict2json(d);
      Log(LOG_INFO, "ws.auth", "*** server sent unparsable hello: %s", jp);
      free( (void *)jp );
   }
   return false;
}


static bool ws_txtframe_dispatch(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_DEBUG, "ws", "txtframe_dispatch: cptr:<%p> d:<%p>", cptr, d);
      return true;
   }
   int i = 0;
   char json_req[65];

   // Pointer to available routes
   struct ws_msg_routes *rp = ws_routes_cli;

   // Walk the table of handlers
   while (rp[i].type) {
      // End of table marker
      if (!rp[i].type && !rp[i].cb) {
         break;
      }
      memset( json_req, 0, sizeof(json_req) );
      snprintf(json_req, sizeof(json_req), "$.%s", rp[i].type);

      // see if this exists in the json
      const char *data = dict_get(d, json_req, NULL);
      if ((strcasecmp(rp[i].type, "ping") != 0) &&
          (strcasecmp(rp[i].type, "cat") != 0) && cfg_http_debug_crazy) {
         Log(LOG_CRAZY, "ws.router", "Matched route #%d for message type %s", i, rp[i].type);
      }

      /* Emit a generic event for this raw websocket message type so other parts of the
       * system can listen to socket-level messages without depending on the current
       * in-process handlers. The existing handler is still called afterwards for
       * backward compatibility. */
      char evname[64]; memset( evname, 0, sizeof(evname) );
      snprintf(evname, sizeof(evname), "ws.msg.%s", rp[i].type);
      event_emit_dict(evname, NULL, d);

      /* Call existing handler to preserve current behavior, then free the dict. */
      rp[i].cb(cptr, d);
      return false;
      i++;
   }
   const char *jp = dict2json(d);
   Log(LOG_CRAZY, "ws.router", "No matches for message: %s", jp);
   free( (void *)jp );
   return true;
}

bool ws_binframe_process(const char *data, size_t len) {
   if (!data || len <= 10) {
      // no real packet will EVER be under 10 bytes, even a keep-alive
      Log(LOG_DEBUG, "ws", "binframe_process: data:<%p> len: %d", data, len);

      return true;
   }

#ifdef	DEBUG_WS_BINFRAMES
   char hex[128] = { 0 };
   size_t n = len < 16 ? len : 16;

   for (size_t i = 0 ; i < n ; i++) {
      snprintf(hex + i * 3, sizeof(hex) - i * 3, "%02X ", (unsigned char)data[i]);
   }

   Log(LOG_DEBUG, "http.ws", "binary: %zu bytes, hex: %s", len, hex);
#endif	// DEBUG_WS_BINFRAMES

//   audio_process_frame(data, len);
   return false;
}

//
// Handle a websocket request (see http.c/http_cb for case ev == MG_EV_WS_MSG)
//
#ifdef	USE_MONGOOSE
bool ws_handle_cli(rrconn_t *cptr, struct mg_ws_message *msg) {
   if (!cptr || !msg || !msg->data.buf) {
      Log( LOG_DEBUG, "http.ws", "ws_handle got cptr:<%p> msg <%p> data <%p>", cptr, msg, (msg ? msg->data.buf : NULL) );
      return true;
   }

#ifdef	HTTP_DEBUG_CRAZY
   if (cfg_http_debug_crazy) {
      Log(LOG_CRAZY, "http", "WS msg: %.*s", (int) msg->data.len, msg->data.buf);
   }
#endif	// HTTP_DEBUG_CRAZY

   if (msg->flags & WEBSOCKET_OP_BINARY) {
      // Binary (audio, waterfall, etc) frames
      ws_binframe_process(msg->data.buf, msg->data.len);
   } else {
      // Text (mostly json) frames
      struct mg_str msg_data = msg->data;

      // Copy to a null terminated buffer
      char buf[HTTP_WS_MAX_MSG + 1];
      memset( buf, 0, sizeof(buf) );
      memcpy(buf, msg_data.buf, msg_data.len);

      Log(LOG_CRAZY, "rrproto.cli.main", "ws_handle_cli: msg=%s", buf);
      dict *d = json2dict(buf);
      ws_txtframe_dispatch(cptr, d);
      memset( buf, 0, sizeof(buf) );
      dict_free(d);
   }
   return false;
}

void http_handler(struct mg_connection *c, int ev, void *ev_data) {
   if (!c) {
      return;
   }

   rrconn_t *cptr = NULL;
   if (c->fn_data) {
      cptr = (rrconn_t *)c->fn_data;
   } else {
      Log(LOG_CRIT, "rrproto.cli.main", "No fn_data in mg_conn:<%p>", c);
      return;
   }

   if (ev == MG_EV_OPEN) {
#ifdef	HTTP_DEBUG_CRAZY
      if (cfg_http_debug_crazy) {
         c->is_hexdumping = 1;
      }
#endif	// HTTP_DEBUG_CRAZY
   } else if (ev == MG_EV_CONNECT) {
      // send the connected event
      dict *d = dict_new();
      dict_add(d, "connected.server", (char *)server_name);
      event_emit_dict("connected", NULL, d);
      dict_free(d);
   } else if (ev == MG_EV_WRITE) {
      // Handle writing audio frames one by one
   } else if (ev == MG_EV_WS_OPEN) {
      const char *this_server = server_name;
      const char *url = get_server_property(this_server, "server.url");

      if (c->is_tls) {
         struct mg_tls_opts opts = {
            .name = mg_url_host(url)
         };

         if (tls_ca_path) {
            opts.ca = tls_ca_path_str;
         } else {
            Log(LOG_CRIT, "ws", "No tls_ca_path set!");
         }
         mg_tls_init(c, &opts);
      }
      ws_connected = 1;

      const char *login_user = get_server_property(this_server, "server.user");
      Log(LOG_DEBUG, "ws", "ev_ws_connect: server: |%s| user: |%s|", server_name, login_user);

      if (!login_user) {
         Log(LOG_CRIT, "ws", "server.user not set in config!");

         return;
      }
      // Let client UI know we are connected (but not logged into!)
      dict *d = dict_new();
      dict_add(d, "auth.user", (char *)login_user);
      dict_add(d, "auth.server", (char *)server_name);
      event_emit_dict("connected", NULL, d);
      dict_free(d);

      ws_send_hello(cptr);
      ws_send_login(cptr, login_user);
   } else if (ev == MG_EV_WS_MSG) {
      struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;

      if (wm) {
         ws_handle_cli(cptr, wm);
      }
   } else if (ev == MG_EV_ERROR) {
      // send (char *)ev_data content
      // { \"error\": { \"msg\":
      ws_connected = 0;

      if (ev_data) {
         dict *d = dict_new();
         dict_add(d, "error.msg", (char *)ev_data);
         event_emit_dict("http.error", NULL, d);
         dict_free(d);
      } else {
         Log(LOG_CRIT, "rrprotocol", "HTTP error! Unknown error");
         event_emit("http.error", NULL, NULL);
      }
   } else if (ev == MG_EV_CLOSE) {
      ws_connected = 0;
      dict *d = dict_new();
      dict_add(d, "disconnected.server", (char *)server_name);
      event_emit_dict("disconnected", NULL, d);
      dict_free(d);
   }
}
#endif // USE_MONGOOSE
void ws_client_init(void) {
   const char *debug = cfg_get_exp("debug.http");

   if (debug && (strcasecmp(debug, "true") == 0 ||
                 strcasecmp(debug, "yes") == 0) ) {
#ifdef	USE_MONGOOSE
      mg_log_set(MG_LL_DEBUG);   // or MG_LL_VERBOSE for even more
#endif	// USE_MONGOOSE
   } else {
#ifdef	USE_MONGOOSE
      mg_log_set(MG_LL_ERROR);
#endif	// USE_MONGOOSE
   }
   free( (void *)debug );
   const char *debug_crazy = cfg_get_exp("debug.http.crazy");

   if (debug_crazy && (strcasecmp(debug_crazy, "true") == 0 ||
                       strcasecmp(debug_crazy, "yes") == 0) ) {
      cfg_http_debug_crazy = true;
   }
   free( (void *)debug_crazy );

#ifdef	USE_MONGOOSE
   mg_mgr_init(&mgr);
#endif	// USE_MONGOOSE
// XXX: Fix this
//   tls_ca_path = find_file_by_list(default_tls_ca_paths,
// sizeof(default_tls_ca_paths) / sizeof(char *));
   if (!tls_ca_path) {
      tls_ca_path = strdup("*");
   }

   if (tls_ca_path) {
#ifdef	USE_MONGOOSE
      // turn it into a mongoose string
      tls_ca_path_str = mg_str(tls_ca_path);
      Log(LOG_DEBUG, "ws", "Setting TLS CA path to <%p> %s with target mg_str at <%p>", tls_ca_path, tls_ca_path,
         tls_ca_path_str);
#endif	// USE_MONGOOSE
   } else {
      Log(LOG_CRIT, "ws", "unable to find TLS CA file");
      exit(1);
   }
   cfg_show_pings = cfg_get_bool("ui.show-pings", false);
   Log(LOG_DEBUG, "ws", "ws_init finished");
}

// XXX: We need to move to a similar arrangement as the client,
// XXX: so these can be properly split across multiple source files
// XXX: and accessed in a pleasant way...
#ifdef	USE_MONGOOSE
struct ws_msg_routes ws_routes[] = {
   { .type = "auth", .cb = ws_handle_auth_msg, .auth_reqd = false },
   { .type = "cat", .cb = ws_handle_rigctl_msg, .auth_reqd = true },
   { .type = "hello", .cb = ws_handle_hello_msg, .auth_reqd = false },
//   { .type = "media", .cb = ws_handle_media_msg, .auth_reqd = true },
   { .type = "ping", .cb = ws_handle_ping_msg, .auth_reqd = false },
//   { .type = "pong",  .cb = ws_handle_pong_msg,  .auth_reqd = false },
//   { .type = "talk",  .cb = ws_handle_talk_msg,  .auth_reqd = true },
//   { .type = "talk.cmd", .cb = ws_handle_talk_cmd, .auth_reqd = false },
//   { .type = "talk.quit", .cb = ws_handle_quit,  .auth_reqd = false },
};
#endif	// USE_MONGOOSE

bool rrproto_ws_connect(int server) {
   return false;
}

#ifdef	USE_MONGOOSE
bool ws_init(struct mg_mgr *mgr) {
   if (!mgr) {
      Log(LOG_CRIT, "ws", "ws_init called with NULL mgr");
      return true;
   }

   Log(LOG_DEBUG, "http.ws", "WebSocket init completed succesfully");
   return false;
}

void ws_fini(struct mg_mgr *mgr) {
   mg_mgr_free(mgr);
}

// Send to a specific, authenticated websocket session
void ws_send_to_cptr(rrconn_t *sender, rrconn_t *cptr, struct mg_str *msg_data, int data_type) {
   if (!cptr || !msg_data) {
      return;
   }
   mg_ws_send(cptr->conn, msg_data->buf, msg_data->len, data_type);
}

// Send to all logged in instances of the user
void ws_send_to_name(rrconn_t *sender, const char *username, struct mg_str *msg_data, int data_type) {
   if (!sender || !username || !msg_data) {
      Log(LOG_CRIT, "ws", "ws_send_to_name passed incomplete data; sender:<%p>, username:<%p>, msg_data:<%p>", sender, username, msg_data);
      return;
   }

   rrconn_t *current = http_client_list;
   while (current) {
      // Messages from the server will have NULL sender
      if (!sender || current->is_ws) {
         ws_send_to_cptr(sender, current, msg_data, data_type);
      }
      current = current->next;
   }
}
#endif // USE_MONGOOSE

bool ws_kick_by_name(const char *name, const char *reason) {
   if (!http_client_list) {
      return true;
   }

   rrconn_t *curr = http_client_list;
   while (curr) {
      if (strcasecmp(name, curr->chatname) == 0) {
         ws_kick_client(curr, reason);
      }
      curr = curr->next;
   }
   return false;
}

bool ws_kick_by_uid(int uid, const char *reason) {
   if (!http_client_list) {
      return true;
   }

   rrconn_t *curr = http_client_list;
   while (curr) {
      if (uid == curr->user->uid) {
         ws_kick_client(curr, reason);
      }
      curr = curr->next;
   }
   return false;
}

bool ws_kick_client(rrconn_t *cptr, const char *reason) {
   // skip freeing resources if no client structure
   if (!cptr) {
      Log( LOG_DEBUG, "auth", "ws_kick_client with NULL cptr and reason: %s", (reason ? reason : "(none)") );
      return true;
   }

   if (!cptr->conn) {
      Log( LOG_DEBUG, "auth", "ws_kick_client for cptr <%p> has mg_conn <%p> and is invalid", cptr,
         (cptr ? cptr->conn : NULL) );
      return true;
   }

   // If we have a client structure attached, release it's resources
   if (cptr->user_agent) {
      free(cptr->user_agent);
      cptr->user_agent = NULL;
   }

   if (cptr->cli_version) {
      free(cptr->cli_version);
      cptr->cli_version = NULL;
   }

   // make sure we're not accessing unsafe memory
   if (cptr->user && cptr->chatname[0] != '\0') {
      if (cptr->active) {
         ws_send_notice(cptr, "You have been kicked from the server: %s", reason);
         // XXX: replace with ws_broadcast_quit(cptr);

         // blorp out a quit to all connected users
         dict *d = dict_new();
         dict_add_ulong(d, "msg.ts", now);
         dict_add(d, "msg.type", "quit");
         dict_add(d, "msg.user", cptr->chatname);
         dict_add(d, "quit.reason", reason);
         dict_add_int(d, "quit.clones", cptr->user->clones - 1);
         ws_broadcast_dict(NULL, d, WEBSOCKET_OP_TEXT);
         dict_free(d);
      }
   }
   // XXX: Delete the user
   return true;
}

#ifdef	USE_MONGOOSE
bool ws_kick_client_by_c(struct mg_connection *c, const char *reason) {
   char resp_buf[HTTP_WS_MAX_MSG + 1];

   if (!c) {
      return true;
   }

   // Tell their client they've been disconnected
   prepare_msg( resp_buf, sizeof(resp_buf), "Client kicked: %s", (reason ? reason : "no reason given") );
   dict *d = dict_new();
   dict_add(d, "auth.error", resp_buf);
   const char *jp = dict2json(d);
   mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_CLOSE);
   event_emit_dict("disconnected", NULL, d);
   dict_free(d);
   free((void *)jp);
   http_remove_client(c);
   free(c);

   return false;
}

static bool ws_handle_pong(rrconn_t *cptr, dict *d) {
   bool rv = false;
   char *ts = NULL;

   if (!cptr || !d) {
      Log( LOG_CRAZY, "http.ws", "ws_handle_pong got cptr:<%p> dict<%p>", cptr, d);
      rv = true;
      goto cleanup;
   }
   char ip[INET6_ADDRSTRLEN];   // Buffer to hold IPv4 or IPv6 address
   int port = 0;

#ifdef	USE_MONGOOSE
   port = cptr->conn->rem.port;

   if (cptr->conn->rem.is_ip6) {
      inet_ntop( AF_INET6, cptr->conn->rem.addr.ip6, ip, sizeof(ip) );
   } else {
      inet_ntop( AF_INET, &cptr->conn->rem.addr.ip4, ip, sizeof(ip) );
   }
#endif	// USE_MONGOOSE

   const char *pong_ts = dict_get(d, "pong.ts", NULL);
   if (!pong_ts) {
      Log(LOG_WARN, "http.ws", "ws_handle_pong: PONG from user with no timestamp");
      rv = true;
      goto cleanup;
   } else {
      Log(LOG_CRAZY, "http.ws", "ws_handle_pong: PONG from user %s with ts:|%s|",
         (*cptr->chatname ? cptr->chatname : "<UNAUTHENTICATED>"), ts);
   }

   char *endptr;
   errno = 0;
   time_t ts_t = strtoll(ts, &endptr, 10);
   if (errno == ERANGE || ts_t < 0 || ts_t > LONG_MAX || *endptr != '\0') {
      Log(LOG_WARN, "http.pong", "Got invalid ts |%s| from client <%p>", ts, cptr);
      rv = true;
      goto cleanup;
   }

   time_t ping_expiry = ts_t + HTTP_PING_TIME;
   if ( (ping_expiry) < now) {
      Log(LOG_AUDIT, "http.pong",
         "Late ping for cptr:<%p> from %s:%d ts: %li + %li (timeout) < now %li", cptr, ip, port,
         ts_t, HTTP_PING_TIMEOUT, now);
      ws_kick_client(cptr, "Network Error: PING expired");
      rv = true;
      goto cleanup;
   } else {
      // The pong response is valid, update the client's data
      cptr->last_heard = now;
      cptr->last_ping = 0;
      cptr->ping_attempts = 0;
      Log(LOG_CRAZY, "http.pong", "Reset user %s last_heard to now:[%li] and last_ping to 0",
         (*cptr->chatname ? cptr->chatname : "<UNAUTHENTICATED>"), now);
   }
cleanup:
   free(ts);

   return rv;
}

// Deal with the binary requests
bool ws_binframe_process_mg(rrconn_t *cptr, const char *buf, size_t len) {
   Log(LOG_DEBUG, "ws.binframe", "Binary frame of %li bytes", len);

   // Here we need to pull out the channel ID and send it the users expecting
   // this codec
   if (len < 8) {
      // This frame is too small to contain meaningful data, discard it
      return true;
   }
   // Copy 4 bytes from the start of the buffer into a NULL-terminated string
   char codec[5];
   memset(codec, 0, 5);
   memcpy(codec, buf, 4);

   // Copy 4 bytes from the buffer into a NULL-terminated string for channel id
   char channel[5];
   memset(channel, 0, 5);
   memcpy(channel, buf, 4);

   // Determine where to send the message, by channel #
   int chan_num = atoi(channel);
   Log(LOG_DEBUG, "ws.binframe", "Got message with codec %s for channel %d", codec, channel);

   return false;
}

//
// Handle a TEXT ws message
//
static bool ws_txtframe_process(rrconn_t *cptr, dict *d) {
   bool result = false;
   bool ping_pong = false;	// ping?/pong! message?
   const char *msg_type = dict_get(d, "msg.type", NULL);
   time_t msg_ts = dict_get_ulong(d, "msg.ts", 0);

   if (!msg_type) {
      // Old protocol
      Log(LOG_CRIT, "rrproto.core", "ws_txtframe_process: msg_type unset!");
      return true;
   }

   if (strcasecmp(msg_type, "alert") == 0) {
      const char *alert_from = dict_get(d, "alert.from", "*** SERVER ***");
   } else if (strcasecmp(msg_type, "error") == 0) {
      const char *error_msg = dict_get(d, "error.msg", NULL);
   } else if (strcasecmp(msg_type, "auth") == 0) {
      // AUTHENTICATION RELATED
      const char *auth_cmd = dict_get(d, "auth.cmd", NULL);
      const char *auth_user = dict_get(d, "auth.user", NULL);
      const char *auth_error = dict_get(d, "auth.error", NULL);
      if (auth_error) {
         Log(LOG_CRIT, "rrproto.auth", "Auth Error: %s", auth_error);
         goto cleanup;
      }

      if (strcasecmp(auth_cmd, "challenge") == 0) {
         const char *auth_nonce = dict_get(d, "auth.nonce", NULL);
         const char *auth_token = dict_get(d, "auth.token", NULL);
         //
      } else if (strcasecmp(auth_cmd, "login") == 0) {
         //
      } else if (strcasecmp(auth_cmd, "pass") == 0) {
         const char *auth_pass = dict_get(d, "auth.pass", NULL);
      }
   } else if (strcasecmp(msg_type, "cat") == 0) {
      // RIG CONTROL/STATE RELATED
      const char *c_cat_cmd = dict_get(d, "cat.cmd", NULL);
   } else if (strcasecmp(msg_type, "hello") == 0) {
      const char *hello_hwver = dict_get(d, "hello.hwver", "generic");
      const char *hello_swver = dict_get(d, "hello.swver", NULL);
   } else if (strcasecmp(msg_type, "media") == 0) {
      // AUDIO/VIDEO MEDIA RELATED
      const char *media_cmd = dict_get(d, "media.cmd", NULL);
   } else if (strcasecmp(msg_type, "ping") == 0) {
      // PING request
      const char *ping = dict_get(d, "ping", NULL);
      time_t ping_ts = dict_get_time_t(d, "ping.ts", 0);

      if (ping_ts) {
         fprintf(stderr, "PING?\n");
         dict *pong = dict_new();
         dict_add(pong, "msg.type", "pong");
         dict_add_ulong(pong, "pong.ts", ping_ts);
         ws_send_dict(NULL, cptr, pong, WEBSOCKET_OP_TEXT);
         dict_free(pong);
      }
      goto cleanup;
   } else if (strcasecmp(msg_type, "pong") == 0) {
      // PING response
      fprintf(stderr, "PONG!\n");
      const char *pong = dict_get(d, "pong", NULL);
   } else if (strcasecmp(msg_type, "quit") == 0) {
      const char *talk_reason = dict_get(d, "quit.reason", NULL);
      int clones = dict_get_int(d, "quit.clones", 0);
   } else if (strcasecmp(msg_type, "talk") == 0) {
      // CHAT RELATED
      const char *talk_cmd = dict_get(d, "talk.cmd", NULL);
      const char *talk_user = dict_get(d, "talk.user", NULL);

//      const char *talk_target = dict_get(d, "talk.args.target", NULL);
   }

   // Update last heard time
   if (!ping_pong) {
      cptr->last_heard = now;
   }

cleanup:
   return result;
}

#if	0

      // Handle pong messages (responses to server-initiated pings)
      time_t pong_ts = dict_get_time_t(d, "pong.ts", 0);
      if (pong_ts && cptr) {
         result = ws_handle_pong(cptr, d);
         cptr->last_ping = 0;
         cptr->ping_attempts = 0;
         Log(LOG_CRAZY, "http.pong", "Received pong from user %s for ts:%li", cptr->chatname, pong_ts);
         goto cleanup;
      }

      if (hello) {
         Log(LOG_DEBUG, "ws", "Got HELLO from client at cptr:<%p>: %s", cptr, hello);
         cptr->cli_version = malloc(HTTP_UA_LEN);

         if (cptr->cli_version) {
            memset(cptr->cli_version, 0, HTTP_UA_LEN);
            snprintf(cptr->cli_version, HTTP_UA_LEN, "%s", hello);
         }
         goto cleanup;
      }

      if (auth_cmd) {
         if (strcasecmp(auth_cmd, "challenge") == 0) {
            const char *ch_nonce = dict_get(d, "auth.nonce", NULL);
            Log(LOG_CRIT, "rrprotocol.cli.main", "challenge: %s", ch_nonce);
         } else if (strcasecmp(cmd, "login") == 0) {
            fprintf(stderr, "LOGIN cmd: %s\n", cmd);
            goto cleanup;
         }
         goto cleanup;
      }


      if (c_cat_cmd) {
         result = ws_handle_rigctl_msg(cptr, d);
         goto cleanup;
      }

      if (talk_cmd) {
         result = ws_handle_chat_msg(cptr, d);
         goto cleanup;
      }

      fprintf(stderr, "cat:<%x>=%s, media:<%x>=%s\n",
         c_cat_cmd, c_cat_cmd, c_cat_media, c_cat_media);
   }
   } else if (mg_json_get(msg_data, "$.media", NULL) > 0) {
      char *media_cmd = dict_get(d, "media.cmd", NULL);
      char *media_codecs = dict_get(d, "media.codecs", NULL);

      // all packets need a command
      if (!media_cmd) {
         return true;
      }

      if (strcasecmp(media_cmd, "capab") == 0) {
         // Capability negotiation
         if (media_codecs) {
            const char *preferred = cfg_get_exp("codecs.allowed");

            if (!preferred) {
               Log(LOG_CRIT, "ws.media", "media.capab needs codecs.allowed set in config!");
               return true;
            }
            char *common = codec_filter_common(preferred, media_codecs);
            free( (char *)preferred );

            if (strlen(common) < 4) {
               free(common);
               return true;
            }
            char def_codec[5];
            memset(def_codec, 0, 5);
            snprintf(def_codec, sizeof(def_codec), "%s", common);
            Log(LOG_INFO, "ws.media",
               "Client %s <%p> supported codecs: %s, my preferred codecs: %s, common codecs: %s, negotiated default codec: %s",
               cptr->chatname, cptr, media_codecs, cfg_get("codecs.allowed"), common, def_codec);
            char msgbuf[HTTP_WS_MAX_MSG + 1];
            dict *d = dict_new();
            dict_add(d, "media.cmd", "isupport");
            dict_add(d, "media.codecs", common);
            dict_add(d, "media.preferred", def_codec);
            dict_add_ulong(d, "media.ts", now);
            Log(LOG_DEBUG, "ws.media", "Sending supported codecs |%s| with preferred |%s| to client |%s|", common,
               def_codec, cptr->chatname);
            ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
            free(common);
         } else {
            Log(LOG_CRIT, "ws.media", "media.capab without payload");
         }
      } else if (strcasecmp(media_cmd, "codec") == 0) {
         if (cptr->chatname[0] == '\0') {
            return true;
         }
         char *media_codec = dict_get(d, "media.codec", NULL);
         char *media_channel = dict_get(d, "media.channel", NULL);

         if (media_codec && strlen(media_codec) == 4) {
            Log(LOG_DEBUG, "ws.media", "Selected %s codec %s.%s for user %s at cptr:<%p>", media_channel, media_codec,
               media_channel, cptr->chatname, cptr);
            struct fwdsp_subproc *codec_tx_subproc = NULL;
            struct fwdsp_subproc *codec_rx_subproc = NULL;

// XXX: Rewrite this to subscribe rx_channels and rx_channels
            if (media_channel) {
               // XXX: Should we store pointers to the subprocs in the user
               // struct? downside is it requires librustyaxe/http.h to include
               // rrserver/fwdsp-mgr.h or move struct fwdsp_subrpco to
               // librustyaxe/fwdsp-shared.h
               if (strcasecmp(media_channel, "tx") == 0) {
                  if (cptr->codec_tx[0] != '\0') {
                     // XXX: Decrease refcnt on old codec
                  }
                  memset( cptr->codec_tx, 0, sizeof(cptr->codec_tx) );
                  memcpy(cptr->codec_tx, media_codec, 4);
                  codec_tx_subproc = fwdsp_find_or_create(cptr->codec_tx, FW_IO_STDIO, true);
                  Log(LOG_DEBUG, "ws.media", "Started fwdsp %s.tx at %p", cptr->codec_tx, codec_tx_subproc);
               } else if (strcasecmp(media_channel, "rx") == 0) {
                  if (cptr->codec_rx[0] != '\0') {
                     // XXX: Decrease refcnt on old codec
                  }
                  memset( cptr->codec_rx, 0, sizeof(cptr->codec_rx) );
                  memcpy(cptr->codec_rx, media_codec, 4);
                  codec_rx_subproc = fwdsp_find_or_create(cptr->codec_rx, FW_IO_STDIO, false);
                  Log(LOG_DEBUG, "ws.media", "Started fwdsp %s.rx at %p", cptr->codec_rx, codec_rx_subproc);
               } else if (strcasecmp(media_channel, "video-rx") == 0) {
                  // NYI
               } else if (strcasecmp(media_channel, "video-tx") == 0) {
                  // NYI
               } else {
                  Log(LOG_CRIT, "ws.media", "invalid channel '%s' for codec message from cptr:<%p>", media_channel,
                     cptr);
               }
            }
         } else {
            Log(LOG_DEBUG, "ws.media", "No codec in media.codec cmd");
         }
      }
#endif	// 0

//
// Handle a websocket request (see http.c/http_cb for case ev == MG_EV_WS_MSG)
//
bool ws_handle(rrconn_t *cptr, struct mg_ws_message *msg) {
   if (!cptr || !msg || !msg->data.buf) {
      Log( LOG_DEBUG, "http.ws", "ws_handle got msg:<%p> c:<%p> data:<%p>", msg, cptr, (msg ? msg->data.buf : NULL) );

      return true;
   }
#if     defined(HTTP_DEBUG_CRAZY) || defined(DEBUG_PROTO)
   // XXX: This should be moved to an option in config perhaps?
   Log(LOG_CRAZY, "http", "WS msg: %.*s", (int) msg->data.len, msg->data.buf);
#endif

   // Binary (audio, waterfall) frames
   if (msg->flags & WEBSOCKET_OP_BINARY) {
      Log(LOG_CRAZY, "ws.binframe", "Incoming Binary frame: %li bytes", msg->data.len);
      ws_binframe_process_mg(cptr, msg->data.buf, msg->data.len);
   } else {
      // Text (mostly json) frames
      Log(LOG_CRAZY, "ws", "Incoming Text frame: %li bytes: %.*s", msg->data.len, msg->data.len, msg->data.buf);
      struct mg_str msg_data = msg->data;
      char buf[HTTP_WS_MAX_MSG + 1];
      memset( buf, 0, sizeof(buf) );
      memcpy(buf, msg_data.buf, msg_data.len);
      fprintf(stderr, "buf(%d): %s(%d)\n", msg_data.len, buf, strlen(buf));
      dict *d = json2dict(buf);
      if (!d) {
         Log(LOG_CRIT, "rrproto.cli.main", "ws_handle: d is null!");
         return true;
      }

      ws_txtframe_process(cptr, d);
      dict_free(d);
      memset(buf, 0, sizeof(buf) );
   }

   return false;
}
#endif // USE_MONGOOSE

///////////////////////////////////////////////////////////////
// Send an error message to the user
bool ws_send_error(rrconn_t *cptr, const char *fmt, ...) {
   if (!fmt) {
      return true;
   }
   char fullmsg[HTTP_WS_MAX_MSG - 55];
   memset( fullmsg, 0, sizeof(fullmsg) );
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(fullmsg, sizeof(fullmsg), fmt, ap);
   char *escaped_msg = escape_html(fullmsg);
   dict *err_msg = dict_new();
   dict_add(err_msg, "error.msg", escaped_msg);
   dict_add_ulong(err_msg, "error.ts", now);
   ws_send_dict(NULL, cptr, err_msg, WEBSOCKET_OP_TEXT);
   free(escaped_msg);
   dict_free(err_msg);

   va_end(ap);
   return false;
}

// Send an alert message to the user
bool ws_send_alert(rrconn_t *cptr, const char *fmt, ...) {
   if (!fmt) {
      return true;
   }
   char fullmsg[HTTP_WS_MAX_MSG - 55];
   memset( fullmsg, 0, sizeof(fullmsg) );

   va_list ap;
   va_start(ap, fmt);
   vsnprintf(fullmsg, sizeof(fullmsg), fmt, ap);
   char *escaped_msg = escape_html(fullmsg);

   dict *alert_msg = dict_new();
   dict_add(alert_msg, "msg.type", "alert");
   dict_add(alert_msg, "alert.msg", escaped_msg);
   dict_add_ulong(alert_msg, "msg.ts", now);
   ws_send_dict(NULL, cptr, alert_msg, WEBSOCKET_OP_TEXT);
   free(escaped_msg);
   dict_free(alert_msg);
   va_end(ap);
   return false;
}

bool ws_send_notice(rrconn_t *cptr, const char *fmt, ...) {
   if (!cptr || !fmt) {
      return true;
   }
   char fullmsg[HTTP_WS_MAX_MSG - 55];
   memset( fullmsg, 0, sizeof(fullmsg) );

   va_list ap;
   va_start(ap, fmt);
   vsnprintf(fullmsg, sizeof(fullmsg), fmt, ap);
   va_end(ap);
   char *escaped_msg = escape_html(fullmsg);
   dict *notice_msg = dict_new();
   dict_add_ulong(notice_msg, "msg.ts", now);
   dict_add(notice_msg, "msg.type", "notice");
   dict_add(notice_msg, "notice.msg", escaped_msg);
   ws_send_dict(NULL, cptr, notice_msg, WEBSOCKET_OP_TEXT);
   free(escaped_msg);
   dict_free(notice_msg);
   return false;
}
