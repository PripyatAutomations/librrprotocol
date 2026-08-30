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

// XXX: Need to remove Content-Type: from these and just store that here
static const char content_type[] = "Content-Type: ";

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

   for (int i = 0 ; i <= items ; i++) {
      // end of table marker?
      if (!http_res_types[i].shortname && !http_res_types[i].msg) {
         break;
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

   if (!msg) {
      return true;
   }
   // Copy URI into null-terminated buffer
   char path[4096];
   memset( path, 0, sizeof(path) );
   snprintf(path, sizeof(path), "%.*s", (int)msg->uri.len, msg->uri.buf);
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
   snprintf(real_path, sizeof(real_path), "%s/%s", www_root, path);

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
   bool rv = false;

   if (!cptr || !d) {
      Log( LOG_CRAZY, "http.ws", "ws_handle_pong got cptr:<%p> dict<%p>", cptr, d);
      rv = true;
      goto cleanup;
   }
   char *ip = cptr->user_ip;
   int port = cptr->user_port;

   time_t msg_ts = dict_get_ulong(d, "msg.ts", 0);
   if (!msg_ts) {
      Log(LOG_WARN, "http.ws", "ws_handle_pong: PONG from user with no timestamp");
      rv = true;
      goto cleanup;
   } else {
      Log(LOG_CRAZY, "http.ws", "ws_handle_pong: PONG from user %s with ts:|%lu|",
         (*cptr->chatname ? cptr->chatname : "<UNAUTHENTICATED>"), msg_ts);
   }

   char *endptr;
   errno = 0;

   time_t ping_expiry = msg_ts + HTTP_PING_TIME;
   if ( (ping_expiry) < now) {
      Log(LOG_AUDIT, "http.pong",
         "Late ping for cptr:<%p> from %s:%d ts: %li + %li (timeout) < now %li", cptr, ip, port,
         msg_ts, HTTP_PING_TIMEOUT, now);
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
      return true;
   }

   if (strcasecmp(msg_type, "alert") == 0) {
      const char *alert_from = dict_get(d, "alert.from", "*** SERVER ***");
   } else if (strcasecmp(msg_type, "error") == 0) {
      const char *error_msg = dict_get(d, "error.msg", NULL);
   } else if (strcasecmp(msg_type, "auth") == 0) {
      ws_handle_auth_msg(cptr, d);
   } else if (strcasecmp(msg_type, "cat") == 0) {
      // RIG CONTROL/STATE RELATED
      const char *c_cat_cmd = dict_get(d, "cat.cmd", NULL);
   } else if (strcasecmp(msg_type, "hello") == 0) {
      const char *hello_hwver = dict_get(d, "hello.hwver", "generic");
      const char *hello_swver = dict_get(d, "hello.swver", NULL);
      Log(LOG_DEBUG, "ws", "Got HELLO from client at cptr:<%p>: swver=%s, hwver=%s", cptr, hello_swver, (hello_hwver ? hello_hwver : "generic"));
      cptr->cli_version = malloc(HTTP_UA_LEN);

      if (cptr->cli_version) {
         memset(cptr->cli_version, 0, HTTP_UA_LEN);
         snprintf(cptr->cli_version, HTTP_UA_LEN, "%s@%s", hello_swver, (hello_hwver ? hello_hwver : "generic"));
      }
   } else if (strcasecmp(msg_type, "media") == 0) {
      // AUDIO/VIDEO MEDIA RELATED
      const char *media_cmd = dict_get(d, "media.cmd", NULL);
   } else if (strcasecmp(msg_type, "ping") == 0) {
      // PING request
      const char *ping = dict_get(d, "ping", NULL);
      time_t ping_ts = dict_get_time_t(d, "msg.ts", 0);
      if (ping_ts) {
         dict *pong = dict_new();
         dict_add(pong, "msg.type", "pong");
         dict_add_ulong(pong, "pong.ts", ping_ts);
         ws_send_dict(NULL, cptr, pong, WEBSOCKET_OP_TEXT);
         dict_free(pong);
      } else {
         // XXX: for now just complain
         Log(LOG_DEBUG, "srv.http", "PING with no TS from cptr:<%p>", cptr);
      }
      goto cleanup;
   } else if (strcasecmp(msg_type, "pong") == 0) {
      if (msg_ts && cptr) {
         result = ws_handle_pong(cptr, d);
         cptr->last_ping = 0;
         cptr->ping_attempts = 0;
         Log(LOG_CRAZY, "http.pong", "Received pong from user %s for ts:%lu", cptr->chatname, msg_ts);
         goto cleanup;
      }
   } else if (strcasecmp(msg_type, "rigctl") == 0) {
      result = ws_handle_rigctl_msg(cptr, d);
   } else if (strcasecmp(msg_type, "quit") == 0) {
      const char *talk_reason = dict_get(d, "quit.reason", NULL);
      int clones = dict_get_int(d, "quit.clones", 0);
   } else if (strcasecmp(msg_type, "talk") == 0) {
      // CHAT RELATED
         result = ws_handle_chat_msg(cptr, d);
   }

   // Update last heard time
   if (!ping_pong) {
      cptr->last_heard = now;
   }

cleanup:
   return result;
}

#if	0
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
// Handle a websocket request
//
bool ws_handle(rrconn_t *cptr, struct mg_ws_message *msg) {
   if (!cptr || !msg || !msg->data.buf) {
      Log( LOG_DEBUG, "http.ws", "ws_handle got msg:<%p> c:<%p> data:<%p>", msg, cptr, (msg ? msg->data.buf : NULL) );

      return true;
   }
#if     defined(HTTP_DEBUG_CRAZY) || defined(DEBUG_PROTO)
   // XXX: This should be moved to an option in config perhaps?
   Log(LOG_CRAZY, "http", "ws_handle WS msg: %.*s", (int) msg->data.len, msg->data.buf);
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
//      fprintf(stderr, "buf(%d): %s(%d)\n", msg_data.len, buf, strlen(buf));
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

///// Main HTTP callback
void ws_http_cb(struct mg_connection *c, int ev, void *ev_data) {
   if (!c) {
      return;
   }
   struct mg_http_message *hm = (struct mg_http_message *) ev_data;

   // Try to find the cptr for this mg_connection
   rrconn_t *cptr = http_find_client_by_c(c);
   if (!cptr) {
      cptr = http_add_client(c, false);
      if (!cptr || !cptr->conn) {
         Log(LOG_CRIT, "ws.core", "ws_http_cb failed to http_add_client(%p)", c);
         return;
      }
      int port = cptr->conn->rem.port;
      char ip[INET6_ADDRSTRLEN];   // Buffer to hold IPv4 or IPv6 address
      memset(ip, 0, INET6_ADDRSTRLEN);

      if (cptr->conn->rem.is_ip6) {
         inet_ntop( AF_INET6, cptr->conn->rem.addr.ip6, ip, sizeof(ip) );
      } else {
         inet_ntop( AF_INET, &cptr->conn->rem.addr.ip4, ip, sizeof(ip) );
      }
   }

   if (ev == MG_EV_OPEN) {
      if (cfg_get_bool("net.http.hex-dump", false) ) {
         cptr->conn->is_hexdumping = 1;
      }
   } else if (ev == MG_EV_CONNECT) {
      if (cptr->conn->is_tls) {
         Log(LOG_DEBUG, "http", "Initializing TLS");
         struct mg_tls_opts opts;
         opts.ca = mg_str("*");
         mg_tls_init(cptr->conn, &opts);
      }
   } else if (ev == MG_EV_ACCEPT) {
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
      rrconn_t *cptr = http_find_client_by_c(c);
      if (!cptr) {
         Log(LOG_CRAZY, "http.core", "ACCEPT: mg_ev_http_msg cptr doesn't exist, creating");
         cptr = http_add_client(c, false);
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
      if (hm && http_dispatch_route(hm, cptr) == true) {
         Log(LOG_CRAZY, "http.core", "fall through to http_static");
         http_static(hm, cptr);
      }
   } else if (ev == MG_EV_WS_OPEN) {
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
      struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;
      ws_handle(cptr, msg);
   } else if (ev == MG_EV_CLOSE) {
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

         if (cptr->user->clones > 0) {
            cptr->user->clones--;
         }

         // reduce the # of clones for the user / reset to 0
         Log(LOG_CRAZY, "http", "Departing user %s had %d clones", cptr->chatname, cptr->user->clones);

         // We want to deal with clones
         if (cptr->user->clones < 0) {
            Log(LOG_CRIT, "http", "Likely bug in %s in %s:%d- cptr->user->clones < 1: %d", __FUNCTION__, __FILE__,
               __LINE__, cptr->user->clones);
         }

         if (cptr->active) {
            // blorp out a quit to all connected users
            dict *rig_msg = dict_new();
            dict_add(rig_msg, "msg.type", "talk");
            dict_add(rig_msg, "talk.cmd", "quit");
            dict_add(rig_msg, "talk.ip", ip);
            dict_add(rig_msg, "talk.reason", "connection closed");
            dict_add(rig_msg, "talk.user", cptr->chatname);
            dict_add_int(rig_msg, "talk.clones", cptr->user->clones);
            dict_add_ulong(rig_msg, "msg.ts", now);
            ws_broadcast_dict(NULL, rig_msg, WEBSOCKET_OP_TEXT);
            dict_free(rig_msg);
            Log(LOG_AUDIT, "auth", "User %s on cptr:<%p> cptr:<%p> from %s:%d disconnected", cptr->chatname, cptr, cptr, ip, port);
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
   }
}
#endif // USE_MONGOOSE

// Combine some common, safe string handling into one call
bool prepare_msg(char *buf, size_t len, const char *fmt, ...) {
   if (!buf || !fmt) {
      return true;
   }
   va_list ap;
   memset(buf, 0, len);
   va_start(ap, fmt);
   vsnprintf(buf, len, fmt, ap);
   va_end(ap);

   return false;
}
