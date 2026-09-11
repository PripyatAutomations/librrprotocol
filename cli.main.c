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
extern bool ws_handler_auth_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_alert_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_client_auth_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_error_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_hello_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_media_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_notice_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_ping_msg(rrconn_t *cptr, dict *d);
extern bool ws_handle_pong_msg(rrconn_t *cptr, dict *d);
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
//   { .type = "irc",   .cb = ws_handle_irc_msg },
   { .type = "media",  .cb = ws_handle_media_msg },
   { .type = "notice", .cb = ws_handle_notice_msg },
   { .type = "ping",   .cb = ws_handle_ping_msg },
   { .type = "pong",   .cb = ws_handle_pong_msg },
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
      Log(LOG_INFO, "auth.ws", "*** server is running %s on %s ***", h_swver, h_hwver);
   } else {
      const char *jp = dict2json(d);
      Log(LOG_INFO, "auth.ws", "*** server sent unparsable hello: %s", jp);
      free( (void *)jp );
   }
   return false;
}

static bool ws_txtframe_dispatch(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_DEBUG, "ws", "txtframe_dispatch: cptr:<%p> d:<%p>", cptr, d);
      return true;
   }
   char json_req[65];
   struct ws_msg_routes *rp = ws_routes_cli;
   const char *msg_type = dict_get(d, "msg.type", NULL);

   // Send an event; messages with no msg.type (e.g. legacy media capab) emit
   // a generic "ws.msg.unknown" event instead of "ws.msg.(null)"
   char evname[64];
   memset( evname, 0, sizeof(evname) );
   snprintf(evname, sizeof(evname), "ws.msg.%s", (msg_type ? msg_type : "unknown"));
   event_emit_dict(evname, NULL, d);

   // Walk the table of handlers
   int i = 0;
   while (rp[i].type) {
      // End of table marker
      if (!rp[i].type && !rp[i].cb) {
         Log(LOG_CRAZY, "ws.cli", "End of route table reached without match for msg_type %s", msg_type);
         break;
      }

      if (msg_type && strcasecmp(rp[i].type, msg_type) == 0) {
         /* Emit a generic event for this raw websocket message type so other parts of the
          * system can listen to socket-level messages without depending on the current
          * in-process handlers. The existing handler is still called afterwards for
          * backward compatibility. */
         // Call the stored handler
         rp[i].cb(cptr, d);
         return false;
      }
      i++;
   }

   // XXX: make this a compile time enable for higher debug levels
   // Dump the dict for debugging purposes
   const char *jp = dict2json(d);
   Log(LOG_CRAZY, "http.ws", "%s: No matches for message: %s", __FUNCTION__, jp);
   free( (void *)jp );
   return true;
}

// Deal with the binary frames we receive from the server
// (audio, waterfall, modem data, etc). See doc/media-frames.md.
bool ws_binframe_process(const char *data, size_t len) {
   if (!data || len < RR_BINFRAME_HDR_LEN) {
      // no real packet will EVER be under the header size, even a keep-alive
      Log(LOG_DEBUG, "ws", "%s: data:<%p> len: %zu", __FUNCTION__, data, len);

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

   struct rr_binframe f;
   int rv = rr_binframe_parse( (const uint8_t *)data, len, &f);

   if (rv < 0) {
      // invalid/unrecognized frame; parse already logged the reason
      return true;
   }
   if (rv > 0) {
      // legacy frame from an old peer: ignore for now
      Log(LOG_DEBUG, "ws.binframe", "Received legacy frame of %zu bytes", len);

      return false;
   }
   // Dispatch by subsystem; fires media.frame.* binary events
   return rr_binframe_dispatch(&f, NULL);
}

#ifdef	USE_MONGOOSE
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
      ws_send_hello(cptr);
      ws_send_login(cptr, login_user);
      dict_free(d);
   } else if (ev == MG_EV_WS_MSG) {
      struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;

      if (!wm) {
         Log(LOG_CRIT, "rrprotocol.ws", "Empty message in MG_EV_WS_MSG");
         return;
      }

      if (wm->flags & WEBSOCKET_OP_BINARY) {
         // Binary (audio, waterfall, etc) frames
         ws_binframe_process(wm->data.buf, wm->data.len);
      } else {
         // Text (mostly json) frames
         struct mg_str msg_data = wm->data;

         // Copy to a null terminated buffer
         char buf[HTTP_WS_MAX_MSG + 1];
         memset( buf, 0, sizeof(buf) );
         memcpy(buf, msg_data.buf, msg_data.len);

         Log(LOG_CRAZY, "http", "ws_handle_cli: msg=%s", buf);
         dict *d = json2dict(buf);
         ws_txtframe_dispatch(cptr, d);
         memset( buf, 0, sizeof(buf) );
         dict_free(d);
      }
      return;
   } else if (ev == MG_EV_ERROR) {
      // send (char *)ev_data content
      // { \"error\": { \"msg\":
      ws_connected = 0;

      // Only act if this is the active connection - a stale connection's
      // error must not close the new one (i.e. /server switching).
      // NB: the client's active rrconn_t is c->fn_data; the library does
      // not reference client connection globals (rrclient/rrclient.c).
      if (cptr && cptr->conn == c) {
         mg_ws_send(c, NULL, 0, WEBSOCKET_OP_CLOSE);

         if (cptr->conn) {
            cptr->conn->is_closing = 1;
         }

         if (ev_data) {
            dict *d = dict_new();
            dict_add(d, "error.msg", (char *)ev_data);
            event_emit_dict("http.error", NULL, d);
            dict_free(d);
         } else {
            Log(LOG_CRIT, "rrprotocol", "HTTP error! Unknown error");
            event_emit("http.error", NULL, NULL);
         }
      }
   } else if (ev == MG_EV_CLOSE) {
      bool active = (cptr && cptr->conn == c);

      ws_connected = 0;

      // Only tear down state and emit "disconnected" if the ACTIVE
      // connection closed. Stale connections (i.e. closed by /server switch)
      // must not nuke the new connection's state or trigger reconnects.
      if (active) {
         cptr->conn = NULL;

         dict *d = dict_new();
         dict_add(d, "msg.type", "auth");
         dict_add(d, "auth.server", server_name);
         event_emit_dict("disconnected", NULL, d);
         dict_free(d);
      }
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

// NB: rrproto_ws_connect() removed - a stub that did nothing; connection
// setup is client behavior and lives in rrclient/ (connman.c, rrclient.c)

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
         dict_add_int(d, "quit.sessions", cptr->user->sessions - 1);
         ws_broadcast_dict(NULL, d, WEBSOCKET_OP_TEXT);
         dict_free(d);
      }
   }
   // XXX: Delete the user
   if (!cptr->conn) {
      Log( LOG_DEBUG, "auth", "ws_kick_client for cptr <%p> has mg_conn <%p> and is invalid", cptr,
         (cptr ? cptr->conn : NULL) );
      return true;
   }

#ifdef	USE_MONGOOSE
   return ws_kick_client_by_c(cptr->conn, reason);
#endif	// USE_MONGOOSE
   return false;
}

#ifdef	USE_MONGOOSE
bool ws_kick_client_by_c(struct mg_connection *c, const char *reason) {
   bool rv = false;
   char resp_buf[HTTP_WS_MAX_MSG + 1];

   if (!c) {
      return true;
   }

   // Tell their client they've been disconnected
   prepare_msg( resp_buf, sizeof(resp_buf), "Client kicked: %s", (reason ? reason : "no reason given") );
   dict *d = dict_new();
   dict_add(d, "msg.type", "auth");
   dict_add(d, "auth.error", resp_buf);
   const char *jp = dict2json(d);
   // Rewrite this to use ws_send_dict();
   mg_ws_send(c, jp, strlen(jp), WEBSOCKET_OP_TEXT);
   mg_ws_send(c, NULL, 0, WEBSOCKET_OP_CLOSE);
   c->is_closing = 1;
   event_emit_dict("disconnected", NULL, d);
   dict_free(d);
   free((void *)jp);
   return rv;
}
#endif // USE_MONGOOSE

// Deal with the binary requests from a server-side perspective
bool ws_binframe_process_mg(rrconn_t *cptr, const char *buf, size_t len) {
   if (!cptr || !buf || len < RR_BINFRAME_HDR_LEN) {
      // This frame is too small to contain meaningful data, discard it
      Log(LOG_DEBUG, "ws.binframe", "%s: dropping short frame (%zu bytes)", __FUNCTION__, len);

      return true;
   }
   Log(LOG_DEBUG, "ws.binframe", "Binary frame of %zu bytes", len);

   struct rr_binframe f;
   int rv = rr_binframe_parse( (const uint8_t *)buf, len, &f);

   if (rv < 0) {
      Log(LOG_DEBUG, "ws.binframe", "Dropping unparseable frame");

      return true;
   }
   if (rv > 0) {
      // Legacy frame from an old client: keep the old behavior of
      // ignoring it until all clients speak binframe v2.
      Log(LOG_DEBUG, "ws.binframe", "Dropping legacy frame of %zu bytes", len);

      return false;
   }
   // The server may only accept media from authenticated users, and
   // only for directions the connection has negotiated a codec for.
   if (!cptr->authenticated) {
      Log(LOG_AUDIT, "auth", "Dropping %zu byte binary frame from unauthenticated client %s on cptr:<%p>",
         len, (cptr->chatname[0] != '\0' ? cptr->chatname : "(unknown)"), cptr);
      return true;
   }
   bool is_tx_frame = (f.hdr.direction == RR_BINFRAME_DIR_TX);
   const char *negotiated = is_tx_frame ? cptr->codec_tx : cptr->codec_rx;

   if (f.hdr.subsystem == RR_BINFRAME_SUBSYS_AUDIO && negotiated[0] == '\0') {
      Log(LOG_DEBUG, "ws.binframe", "Dropping audio frame: no codec negotiated for %s",
         (is_tx_frame ? "tx" : "rx"));

      return true;
   }

   // Media source connections (FLAG_MEDIA_SOURCE, granted via the
   // media.source priv and the media.cmd:source handshake) push frames
   // for the channels they registered. The frame's (subsystem, direction,
   // vfo, rig) must match a channel that connection is subscribed to.
   if (is_tx_frame && client_has_flag(cptr, FLAG_MEDIA_SOURCE) ) {
      struct rr_mediachan *cp = media_chan_find(f.hdr.subsystem, f.hdr.direction,
         f.hdr.vfo, f.hdr.rig);

      if (!cp) {
         Log(LOG_DEBUG, "ws.media", "Dropping source frame: no channel for subsys 0x%02X dir 0x%02X vfo %u rig %u",
            f.hdr.subsystem, f.hdr.direction, f.hdr.vfo, f.hdr.rig);

         return true;
      }
      u_int32_t chan_id = (u_int32_t)(cp - media_channels) + 1;

      if (!chan_id_in_array(cptr->tx_channels, MAX_TX_CHANNELS, chan_id) ) {
         Log(LOG_AUDIT, "ws.media", "Dropping source frame from %s for unsubscribed channel %s",
            cptr->chatname, cp->uuid);

         return true;
      }
      // Accept the frame: the server owns the wire values when it fans out
      // (see doc/media-frames.md); rebuild direction/seq centrally and
      // broadcast to the subscribers of this channel's RX counterpart.
      struct rr_mediachan *rx = media_chan_find(cp->subsystem, RR_BINFRAME_DIR_RX, cp->vfo, cp->rig);

      if (rx) {
         ws_media_broadcast_subscribed(rx, f.data, f.len, f.hdr.codec);
      } else {
         // No RX counterpart (e.g. a TX-only subsystem); dispatch to the
         // event bus so the program can decide what to do with it.
         event_emit_binary("media.frame.audio", cptr, f.data, f.len);
      }
      return false;
   }
   // TX-direction audio from a non-source client is the legacy PTT
   // microphone path; RX-direction frames arriving at the server are
   // bogus - drop both rather than relay them.
   if (is_tx_frame || f.hdr.direction != RR_BINFRAME_DIR_RX) {
      return true;
   }
   // RX-direction frames arriving at the server are not valid client
   // traffic; keep dispatching to the event bus for the program (legacy
   // fwdsp pipe path) until that's fully retired.
   return rr_binframe_dispatch(&f, cptr);
}

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
   dict_add(err_msg, "msg.type", "error");
   dict_add(err_msg, "error.msg", escaped_msg);
   dict_add_ulong(err_msg, "msg.ts", now);
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
   dict_add_ulong(alert_msg, "alert.ts", now);   // clients read alert.ts (see send_global_alert)
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

   dict *notice_msg = dict_new();
   dict_add_ulong(notice_msg, "msg.ts", now);
   dict_add(notice_msg, "msg.type", "notice");
   dict_add(notice_msg, "notice.msg", fullmsg);
   ws_send_dict(NULL, cptr, notice_msg, WEBSOCKET_OP_TEXT);
   dict_free(notice_msg);
   return false;
}
