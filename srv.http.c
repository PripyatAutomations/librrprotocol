// librrprotocol/srv.http.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Here we deal with http requests using mongoose
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
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

extern time_t now;

// This defines a hard-coded fallback path for httpd root, if not set in config
#ifdef	HOST_POSIX
#ifndef	INSTALL_PREFIX
#define	WWW_ROOT_FALLBACK "./www"
#define	WWW_404_FALLBACK "./www/404.html"
#endif // !INSTALL_PREFIX
#else
#define	WWW_ROOT_FALLBACK "fs:www/"
#define	WWW_404_FALLBACK "fs:www/404.html"
#endif // HOST_POSIX.else

char www_root[PATH_MAX];
char www_fw_ver[128];
char www_headers[32768];
char www_404_path[PATH_MAX];
rrconn_t *http_client_list = NULL;

#if     defined(USE_MONGOOSE)
extern struct mg_mgr mg_mgr;
extern struct mg_tls_opts tls_opts;
#endif // USE_MONGOOSE

static const char s_content_type[] = "Content-Type: ";
static struct http_res_types http_res_types[] = {
   { "7z", "application/x-7z-compressed\r\n" },
   { "css", "text/css\r\n" },
   { "htm", "text/html\r\n" },
   { "html", "text/html\r\n" },
   { "ico", "image/x-icon\r\n" },
   { "js", "application/javascript\r\n" },
   { "json", "application/json\r\n" },
   { "jpg", "image/jpeg\r\n" },
   { "mp3", "audio/mpeg\r\n" },
   { "ogg", "audio/ogg\r\n" },
   { "otf", "font/otf\r\n" },
   { "png", "image/png\r\n" },
   { "svg", "image/svg\r\n" },
   { "tar", "application/x-tar\r\n" },
   { "ttf", "font/ttf\r\n" },
   { "txt", "text/plain\r\n" },
   { "wasm", "application/wasm\r\n" },
   { "webp", "image/webp\r\n" },
   { "woff", "font/woff\r\n" },
   { "woff2", "font/woff2\r\n" },
   { "zip", "application/zip\r\n" },
   { NULL, NULL }
};

// Perform various checks on synthesized URLs to make sure the user isn't up to
// anything shady...
bool check_url(const char *path) {
   if (!path) {
      return true;
   }
   if (strstr(path, "..")) {
      return true;
   }
   return false;
}

// Returns HTTP Content-Type for the chosen short name (save some memory)
const char *http_content_type(const char *type) {
   if (!type) {
      return NULL;
   }
   int items = (sizeof(http_res_types) / sizeof(struct http_res_types) );

   for (int i = 0 ; i < items ; i++) {
      // end of table marker?
      if (!http_res_types[i].shortname && !http_res_types[i].msg) {
         break;
      }
      if (!http_res_types[i].shortname || !http_res_types[i].msg) {
         continue;
      }

      // compare the short name
      if (strcasecmp(http_res_types[i].shortname, type) == 0) {
         return http_res_types[i].msg;
      }
   }

   return "text/plain\r\n";
}

#ifdef	USE_MONGOOSE
bool http_static(struct mg_http_message *msg, rrconn_t *cptr) {
   struct mg_http_serve_opts opts = http_opts;

   if (!msg || !cptr || !cptr->conn || !msg->uri.buf) {
      return true;
   }
   // Copy URI into null-terminated buffer
   char path[4096];
   memset( path, 0, sizeof(path) );
   int path_len = snprintf(path, sizeof(path), "%.*s", (int)(msg->uri.len > INT_MAX ? INT_MAX : msg->uri.len), msg->uri.buf);
   if (path_len < 0 || (size_t)path_len >= sizeof(path) || check_url(path)) {
      Log(LOG_WARN, "http.core", "Rejecting unsafe or oversized static path");
      return true;
   }
   char real_path[8192];
   memset( real_path, 0, sizeof(real_path) );

   if (www_root[0] == '\0') {
      Log(LOG_CRIT, "http.core", "www_root is NULL");
      return true;
   }

   if (strlen(path) == 1 && path[0] == '/') {
      memset( path, 0, sizeof(path) );
      snprintf(path, sizeof(path), "index.html");
   }
   int real_len = snprintf(real_path, sizeof(real_path), "%s/%s", www_root, path);
   if (real_len < 0 || (size_t)real_len >= sizeof(real_path)) {
      Log(LOG_WARN, "http.core", "Rejecting oversized static path");
      return true;
   }

   if (file_exists(real_path) ) {
      // Find last '.' in the path for the extension
      const char *ext = strrchr(path, '.');

      if (ext && *(ext + 1) ) {
         // lookup the mime type based on extension
         const char *ctype = http_content_type(ext + 1);
         char typebuf[256];
         // save it in a form mongoose likes
         memset( typebuf, 0, sizeof(typebuf) );
         snprintf(typebuf, sizeof(typebuf), "%s=%s", ext + 1, ctype);
         // tell mongoose about it
         opts.mime_types = ctype;
         // and serve the file
         mg_http_serve_dir(cptr->conn, msg, &opts);

         return false;
      }
   } else if (is_dir(real_path) ) {
      mg_http_serve_dir(cptr->conn, msg, &opts);

      return false;
   } else {
      // file not found
      Log(LOG_DEBUG, "http.core", "Static dispatch for %s returning 404", path);
      mg_http_serve_file(cptr->conn, msg, www_404_path, &opts);
   }

   return true;
}

static bool ws_handle_pong(rrconn_t *cptr, dict *d) {
   bool rv = true;

   if (!cptr || !d) {
      Log( LOG_CRAZY, "http.ws", "ws_handle_pong got cptr:<%p> dict<%p>", cptr, d);
      rv = false;
      goto cleanup;
   }
   char *ip = cptr->user_ip;
   int port = cptr->user_port;

   time_t msg_ts = dict_get_ulong(d, "msg.ts", 0);
   if (!msg_ts) {
      Log(LOG_WARN, "http.ws", "ws_handle_pong: PONG from user with no timestamp");
      rv = false;
      goto cleanup;
   } else {
      Log(LOG_CRAZY, "http.ws", "ws_handle_pong: PONG from user %s with ts:|%lu|",
         (*cptr->chatname ? cptr->chatname : "<UNAUTHENTICATED>"), msg_ts);
   }

   // RTT measurement: echo the monotonic ping.ts (real microseconds) back and diff it here
   long long ping_mono = dict_get_llong(d, "ping.ts", 0);
   if (ping_mono) {
      long long rtt_us = mono_us() - ping_mono;
      if (rtt_us < 0) {
         rtt_us = 0;              // shouldn't happen on a monotonic clock
      }
      long long rtt = rtt_us / 1000;
      cptr->latency_ms = (int)rtt;
      last_ping_rtt_ms = rtt;
      Log(LOG_INFO, "ping", "RTT to user %s: %lld ms (%lld us) (global last_ping_rtt_ms=%lld)",
          cptr->chatname, rtt, rtt_us, last_ping_rtt_ms);

      // Let higher layers (audio/etc) track latency
      dict *lat = dict_new();
      dict_add_llong(lat, "latency.rtt", rtt);
      dict_add_llong(lat, "latency.rtt_us", rtt_us);
      event_emit_dict("latency", cptr, lat);
      dict_free(lat);
   }

   if (msg_ts > now || now - msg_ts > HTTP_PING_TIME) {
      Log(LOG_DEBUG, "http.pong",
         "Late ping for cptr:<%p> from %s:%d ts: %li + %li (timeout) < now %li", cptr, ip, port,
         msg_ts, HTTP_PING_TIMEOUT, now);
      ws_kick_client(cptr, "Network Error: PING expired");
      rv = false;
      goto cleanup;
   } else {
      cptr->last_heard = now;
      // The pong response is valid, update the client's data
      cptr->last_ping = 0;
      cptr->ping_attempts = 0;

      Log(LOG_CRAZY, "http.pong", "Reset user %s last_heard to now:[%li] and last_ping to 0",
         (*cptr->chatname ? cptr->chatname : "<UNAUTHENTICATED>"), now);
   }

cleanup:
   return rv;
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
      dict_dump(d, stderr);
      ws_send_error(cptr, "Invalid command: <missing msg.type>");
      return false;
   }

   // Unauthenticated clients may only send auth commands (login/pass), pong
   // (in reply to the server's own keep-alive pings) and hello (client
   // version negotiation on connect). Everything else - including client
   // pings, which are an easy DoS/amplification vector - is denied.
   // PARITY: rustyrig-www/js/webui.js (send_ping / webui.auth.js login flow)
   if (!cptr->authenticated &&
       strcasecmp(msg_type, "auth") != 0 &&
       strcasecmp(msg_type, "pong") != 0 &&
       strcasecmp(msg_type, "hello") != 0) {
      Log(LOG_AUDIT, "auth", "Denied %s from unauthenticated client %s on cptr:<%p> from %s:%d",
         msg_type, (cptr->chatname[0] != '\0' ? cptr->chatname : "(unknown)"), cptr, cptr->user_ip, cptr->user_port);

      // Don't reply to ping at all (no amplification); tell the client
      // why anything else was rejected.
      if (strcasecmp(msg_type, "ping") != 0) {
         ws_send_error(cptr, "Not authenticated");
      }
      goto cleanup;
   }

   if (strcasecmp(msg_type, "alert") == 0) {
      const char *alert_from = dict_get(d, "alert.from", "*** SERVER ***");
      (void)alert_from;
      result = true;
   } else if (strcasecmp(msg_type, "error") == 0) {
      const char *error_msg = dict_get(d, "error.msg", NULL);
      (void)error_msg;
      result = true;
   } else if (strcasecmp(msg_type, "auth") == 0) {
      result = ws_handle_auth_msg(cptr, d);
   } else if (strcasecmp(msg_type, "cat") == 0) {
      // RIG CONTROL/STATE RELATED
      // If this msg contains a cat.cmd it's a client command (freq/mode/ptt
      // etc) - route it to the rigctl handler. Messages without a cat.cmd
      // are state broadcasts from the server and don't need processing here.
      if (dict_get(d, "cat.cmd", NULL) ) {
         result = ws_handle_rigctl_msg(cptr, d);
      }
   } else if (strcasecmp(msg_type, "hello") == 0) {
      const char *hello_hwver = dict_get(d, "hello.hwver", "generic");
      const char *hello_swver = dict_get(d, "hello.swver", NULL);
      Log(LOG_DEBUG, "ws", "Got HELLO from client at cptr:<%p>: swver=%s, hwver=%s", cptr, hello_swver, (hello_hwver ? hello_hwver : "generic"));
      free(cptr->cli_version);
      cptr->cli_version = malloc(HTTP_UA_LEN);

      if (cptr->cli_version) {
         memset(cptr->cli_version, 0, HTTP_UA_LEN);
         snprintf(cptr->cli_version, HTTP_UA_LEN, "%s@%s", (hello_swver ? hello_swver : "unknown"),
            (hello_hwver ? hello_hwver : "generic"));
      }
      // hello.role marks connections which are not ordinary chat users (e.g.
      // video-source webcams); they get their own flag and are excluded from
      // the user list so they don't show up alongside normal users.
      // PARITY: librrprotocol/cli.main.c ws_send_hello() (client side sender)
      const char *hello_role = dict_get(d, "hello.role", NULL);

      if (hello_role && strcasecmp(hello_role, "video-source") == 0) {
         client_set_flag(cptr, FLAG_VIDEO_SOURCE);
         Log(LOG_INFO, "ws", "Client at cptr:<%p> announced hello.role: video-source", cptr);
      }
      event_emit_dict("hello", cptr, d);
      result = (cptr->cli_version != NULL);
   } else if (strcasecmp(msg_type, "media") == 0) {
      // AUDIO/VIDEO MEDIA RELATED. media.cmd values are handled by the
      // codec negotiation (cli/srv media handlers) and the media channel
      // (subscribe) protocol in ws.mediachan.c
      const char *media_cmd = dict_get(d, "media.cmd", NULL);

      if (!media_cmd) {
         Log(LOG_DEBUG, "ws.media", "media message without media.cmd from %s", cptr->chatname);
         goto cleanup;
      }
      // Channel subscription commands: list/subscribe/unsubscribe and the
      // media source registration. capab/codec/isupport are negotiated in
      // codecneg/ws.media paths.
      if (strcasecmp(media_cmd, "list") == 0 ||
          strcasecmp(media_cmd, "subscribe") == 0 ||
          strcasecmp(media_cmd, "unsubscribe") == 0 ||
          strcasecmp(media_cmd, "source") == 0 ||
          strcasecmp(media_cmd, "codec") == 0) {
         result = ws_handle_mediachan_msg(cptr, d);
         goto cleanup;
      }
      Log(LOG_WARN, "ws.media", "Invalid media command |%s| from %s", media_cmd, cptr->chatname);
      ws_send_error(cptr, "Invalid command: media.%s", media_cmd);
   } else if (strcasecmp(msg_type, "ping") == 0) {
      // PING request
      const char *ping = dict_get(d, "ping", NULL);
      time_t ping_ts = dict_get_time_t(d, "msg.ts", 0);
      if (ping_ts) {
         dict *pong = dict_new();
         dict_add(pong, "msg.type", "pong");
         dict_add_ulong(pong, "msg.ts", ping_ts);
         ws_send_dict(NULL, cptr, pong, WEBSOCKET_OP_TEXT);
         dict_free(pong);
         result = true;
      } else {
         // XXX: for now just complain
         Log(LOG_DEBUG, "srv.http", "PING with no TS from cptr:<%p>", cptr);
      }
      goto cleanup;
   } else if (strcasecmp(msg_type, "pong") == 0) {
      if (msg_ts && cptr) {
         result = ws_handle_pong(cptr, d);
         goto cleanup;
      }
   } else if (strcasecmp(msg_type, "rigctl") == 0) {
      result = ws_handle_rigctl_msg(cptr, d);
   } else if (strcasecmp(msg_type, "rehash") == 0) {
      // Reload server config & user db. Restricted to admin/owner privs.
      // PARITY: rustyrig-www/js/webui (send msg.type:rehash on /rehash)
      if (!cptr->authenticated || !cptr->user ||
          !(has_priv(cptr->user->uid, "admin|owner") ) ) {
         Log(LOG_AUDIT, "auth", "Denied rehash request from %s on cptr:<%p> from %s:%d",
            (cptr->chatname[0] != '\0' ? cptr->chatname : "(unauthenticated)"), cptr, cptr->user_ip, cptr->user_port);
         dict *err = dict_new();
         dict_add(err, "msg.type", "error");
         dict_add(err, "error.msg", "You don't have permission to rehash");
         dict_add_ulong(err, "msg.ts", now);
         ws_send_dict(NULL, cptr, err, WEBSOCKET_OP_TEXT);
         dict_free(err);
         goto cleanup;
      }
      Log(LOG_INFO, "http.ws", "Rehash requested by %s", cptr->chatname);
      event_emit_dict("rehash", cptr, d);
      result = true;
   } else if (strcasecmp(msg_type, "quit") == 0) {
      const char *talk_reason = dict_get(d, "quit.reason", NULL);
      int sessions = dict_get_int(d, "quit.sessions", 0);
      (void)talk_reason;
      (void)sessions;
      result = true;
   } else if (strcasecmp(msg_type, "talk") == 0) {
      // CHAT RELATED
         result = ws_handle_chat_msg(cptr, d);
   } else {
      Log(LOG_WARN, "http.ws", "Invalid command |%s| from %s", msg_type,
         (cptr->chatname[0] ? cptr->chatname : "(unknown)"));
      ws_send_error(cptr, "Invalid command: %s", msg_type);
      result = false;
   }

   // Update last heard time
   if (!ping_pong) {
      cptr->last_heard = now;
   }

cleanup:
   return result;
}

//
// Handle a websocket request
//
bool ws_handle(rrconn_t *cptr, struct mg_ws_message *msg) {
   if (!cptr || !msg || !msg->data.buf) {
      Log( LOG_DEBUG, "http.ws", "ws_handle got msg:<%p> c:<%p> data:<%p>", msg, cptr, (msg ? msg->data.buf : NULL) );

      return false;
   }
#if     defined(HTTP_DEBUG_CRAZY) || defined(DEBUG_PROTO)
   // XXX: This should be moved to an option in config perhaps?
   Log(LOG_CRAZY, "http", "ws_handle WS msg: %.*s",
      (int)(msg->data.len > INT_MAX ? INT_MAX : msg->data.len), msg->data.buf);
#endif

   // Binary (audio, waterfall) frames
   if (msg->flags & WEBSOCKET_OP_BINARY) {
      Log(LOG_CRAZY, "ws.frame.bin", "Incoming Binary frame: %zu bytes", msg->data.len);
      return ws_binframe_process_mg(cptr, msg->data.buf, msg->data.len);
   } else {
      // Text (mostly json) frames
      Log(LOG_CRAZY, "ws.frame.txt", "Incoming Text frame: %zu bytes: %.*s", msg->data.len,
         (int)(msg->data.len > INT_MAX ? INT_MAX : msg->data.len), msg->data.buf);

      // Drop oversized frames: copying into our fixed buffer without this
      // check smashed the stack/heap and later crashed mg_iobuf_free
      // ("double free or corruption") when the connection closed.
      if (msg->data.len > HTTP_WS_MAX_MSG) {
         Log(LOG_WARN, "http.ws", "Dropping oversized WS text frame (%zu bytes > %d) from cptr:<%p>",
            msg->data.len, HTTP_WS_MAX_MSG, cptr);
         return false;
      }

      struct mg_str msg_data = msg->data;
      char buf[HTTP_WS_MAX_MSG + 1];
      memset( buf, 0, sizeof(buf) );
      memcpy(buf, msg_data.buf, msg_data.len);
//      fprintf(stderr, "buf(%d): %s(%d)\n", msg_data.len, buf, strlen(buf));
      dict *d = json2dict(buf);
      if (!d) {
         Log(LOG_CRIT, "rrproto.cli.main", "ws_handle: d is null!");
         return false;
      }

      bool result = ws_txtframe_process(cptr, d);
      dict_free(d);
      memset(buf, 0, sizeof(buf) );
      return result;
   }
   return false;
}

///// Main HTTP callback
void ws_http_cb(struct mg_connection *c, int ev, void *ev_data) {
   if (!c) {
      return;
   }
   struct mg_http_message *hm = (struct mg_http_message *) ev_data;

   // Try to find the cptr for this mg_connection. We only create one on
   // connection-establishing events (OPEN/ACCEPT/HTTP_MSG) - creating one
   // for ANY event re-added clients after MG_EV_CLOSE freed them, leaking
   // unauthenticated entries.
   rrconn_t *cptr = http_find_client_by_c(c);

   if (ev == MG_EV_OPEN) {
      // The listening socket itself fires MG_EV_OPEN when mg_http_listen()
      // creates it. It is not a client - never register it, it never closes
      // and would otherwise linger as a permanent UNAUTHENTICATED entry.
      if (c->is_listening) {
         return;
      }

      if (!cptr) {
         cptr = http_add_client(c, false);
      }

      if (cptr && cfg_get_bool("net.http.hex-dump", false) ) {
         cptr->conn->is_hexdumping = 1;
      }
   } else if (ev == MG_EV_CONNECT) {
      if (!cptr) {
         Log(LOG_CRIT, "ws.core", "ws_http_cb MG_EV_CONNECT with no client for conn:<%p>", c);
         return;
      }

      if (cptr->conn->is_tls) {
         Log(LOG_DEBUG, "http", "Initializing TLS");
         struct mg_tls_opts opts;
         opts.ca = mg_str("*");
         mg_tls_init(cptr->conn, &opts);
      }
   } else if (ev == MG_EV_ACCEPT) {
      if (c->is_listening) {  // defensive: listeners never fire ACCEPT
         return;
      }
      if (!cptr) {
         cptr = http_add_client(c, false);
      }

      if (!cptr) {
         Log(LOG_CRIT, "ws.core", "ws_http_cb failed to http_add_client(%p)", c);
         return;
      }
      char *ip = cptr->user_ip;
      int port = cptr->user_port;
      Log(LOG_CRAZY, "http", "Accepted connection on cptr:<%p> from %s:%d", cptr, ip, port);

#ifdef	HTTP_USE_TLS
      if (cptr && cptr->conn && cptr->conn->fn_data) {
         Log(LOG_CRAZY, "http", "Init TLS for cptr:<%p> from %s:%d", cptr, ip, port);
         mg_tls_init(cptr->conn, &tls_opts);
      }
#endif	// HTTP_USE_TLS
   } else if (ev == MG_EV_HTTP_MSG) {
      if (!cptr) {
         Log(LOG_CRAZY, "http.core", "ACCEPT: mg_ev_http_msg cptr doesn't exist, creating");
         cptr = http_add_client(c, false);
      }

      if (!cptr) {
         Log(LOG_CRIT, "ws.core", "ws_http_cb failed to http_add_client(%p)", c);
         return;
      }

      // Save the user-agent the first time
      if (!cptr->user_agent) {
         if (hm) {
            struct mg_str *ua_hdr = mg_http_get_header(hm, "User-Agent");

            if (ua_hdr) {
               size_t ua_len = ua_hdr->len < HTTP_UA_LEN ? ua_hdr->len : HTTP_UA_LEN;

               // allocate the memory
               cptr->user_agent = malloc(ua_len);

               if (!cptr->user_agent) {
                  fprintf(stderr, "OOM in http_cb EV_HTTP_MSG\n");
                  abort();
                  return;
               }
               memset(cptr->user_agent, 0, ua_len);
               memcpy(cptr->user_agent, ua_hdr->buf, ua_len);
               Log(LOG_DEBUG, "http.core", "New session cptr:<%p> User-Agent: %s (%d)", cptr,
                  (cptr->user_agent ? cptr->user_agent : "none"), ua_len);
            }
         }
      }

      // Send the request to our HTTP router
      if (hm && !http_dispatch_route(hm, cptr)) {
         Log(LOG_CRAZY, "http.core", "fall through to http_static");
         http_static(hm, cptr);
      }
   } else if (ev == MG_EV_WS_OPEN) {
      if (!cptr) {
         Log(LOG_CRIT, "ws.core", "ws_http_cb MG_EV_WS_OPEN with no client for conn:<%p>", c);
         return;
      }
      char *ip = cptr->user_ip;
      int port = cptr->user_port;

      Log(LOG_CRAZY, "http.core", "WS OPEN for cptr:<%p>", cptr);
      Log(LOG_DEBUG, "http", "Conn cptr:<%p> from %s:%d upgraded to ws with cptr:<%p>", cptr, ip, port, cptr);
      cptr->is_ws = true;
      dict *d = dict_new();
      dict_add(d, "msg.type", "hello");
      dict_add_ulong(d, "msg.ts", now);
      dict_add(d, "hello.swver", VERSION);
      dict_add(d, "hello.hwver", HARDWARE);
      ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
      dict_free(d);
   } else if (ev == MG_EV_WS_MSG) {
      if (!cptr) {
         Log(LOG_CRIT, "ws.core", "ws_http_cb MG_EV_WS_MSG with no client for conn:<%p>", c);
         return;
      }
      struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;
      ws_handle(cptr, msg);
   } else if (ev == MG_EV_CLOSE) {
      if (!cptr) {
         // Already removed (or never added) - nothing to clean up
         return;
      }
      char resp_buf[HTTP_WS_MAX_MSG + 1];
      const char *ip = cptr ? cptr->user_ip : "(unknown)";
      Log(LOG_DEBUG, "http", "http_cb MG_EV_CLOSE for cptr:<%p> ip:%s", cptr, ip);

      // make sure we're not accessing unsafe memory
      if (cptr && cptr->user && cptr->chatname[0] != '\0') {
         char *ip = cptr->user_ip;
         int port = cptr->user_port;

         // Does the user hold PTT? if so turn it off
         if (cptr->is_ptt) {
            cptr->is_ptt = false;
            dict *rig_msg = dict_new();
            dict_add(rig_msg, "msg.type", "cat");
            dict_add(rig_msg, "cat.cmd", "ptt");
            dict_add_bool(rig_msg, "cat.ptt", false);
            dict_add(rig_msg, "cat.user", cptr->chatname);
            if (cptr->ptt_vfo) {
               char vfo_buf[2] = { cptr->ptt_vfo, '\0' };
               dict_add(rig_msg, "cat.vfo", vfo_buf);
            }
            // send it to rrserver to turn off ptt
            event_emit_dict("rig.ptt", NULL, rig_msg);
            dict_free(rig_msg);
         }

         // Free the resources, if any, for the user_agent
         if (cptr->user_agent) {
            free(cptr->user_agent);
            cptr->user_agent = NULL;
         }

         if (cptr->cli_version) {
            free(cptr->cli_version);
            cptr->cli_version = NULL;
         }

         // sessions are decremented in http_remove_client() (srv.client.c) when
         // the client is unlinked from the list; doing it here as well caused
         // a double decrement on authenticated WS clients.
         Log(LOG_CRAZY, "http", "Departing user %s had %d sessions", cptr->chatname, cptr->user->sessions);

         // We want to deal with sessions
         if (cptr->user->sessions < 0) {
            Log(LOG_CRIT, "http", "Likely bug in %s in %s:%d- cptr->user->sessions < 1: %d", __FUNCTION__, __FILE__,
               __LINE__, cptr->user->sessions);
         }

         if (cptr->active) {
            // blorp out a quit to all connected users
            dict *rig_msg = dict_new();
            dict_add(rig_msg, "msg.type", "talk");
            dict_add(rig_msg, "talk.cmd", "quit");
            dict_add(rig_msg, "talk.ip", ip);
            dict_add(rig_msg, "talk.reason", "connection closed");
            dict_add(rig_msg, "talk.user", cptr->chatname);
            dict_add_int(rig_msg, "talk.sessions", cptr->user->sessions);
            dict_add_ulong(rig_msg, "msg.ts", now);
            ws_broadcast_dict(NULL, rig_msg, WEBSOCKET_OP_TEXT);
            dict_free(rig_msg);
            Log(LOG_AUDIT, "auth", "User %s on cptr:<%p> cptr:<%p> from %s:%d disconnected (%d sessions)", cptr->chatname, cptr, cptr, ip, port, cptr->user->sessions);
         }
      } else {
         if (!cptr) {
            Log(LOG_CRIT, "ws.core", "ws_http_cb(): cptr is null!?");
            return;
         }
         char *ip = cptr->user_ip;
         int port = cptr->user_port;

         // This one makes a BUNCH of noise due to webui loading
         Log(LOG_CRAZY, "auth", "Unauthenticated client on cptr:<%p> from %s:%d disconnected", cptr, ip, port);
      }
      if (cptr->conn) {
         http_remove_client(cptr->conn);
      }
      // The departing client's TX subscription/capability no longer
      // constrains shared channel codec selection. Re-announce channel state
      // to the remaining clients after it has been removed from the list.
      media_send_available_all(NULL);
   }
}
#endif // USE_MONGOOSE

// Combine some common, safe string handling into one call
bool prepare_msg(char *buf, size_t len, const char *fmt, ...) {
   if (!buf || !fmt) {
      return false;
   }
   va_list ap;
   memset(buf, 0, len);
   va_start(ap, fmt);
   vsnprintf(buf, len, fmt, ap);
   va_end(ap);

   return true;
}
