// http.c
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
#if     defined(HOST_POSIX)
#if     !defined(INSTALL_PREFIX)
#define	WWW_ROOT_FALLBACK "./www"
#define	WWW_404_FALLBACK "./www/404.html"
#endif // !defined(INSTALL_PREFIX)
#else
#define	WWW_ROOT_FALLBACK "fs:www/"
#define	WWW_404_FALLBACK "fs:www/404.html"
#endif // defined(HOST_POSIX).else

char www_root[PATH_MAX];
char www_fw_ver[128];
char www_headers[32768];
char www_404_path[PATH_MAX];
rrconn_t *http_client_list = NULL;

#if     defined(USE_MONGOOSE)
extern struct mg_mgr mg_mgr;
extern struct mg_tls_opts tls_opts;
#endif // defined(USE_MONGOOSE)

// XXX: Need to remove Content-Type: from these and just store that here
static const char content_type[] = "Content-Type: ";

static struct http_res_types http_res_types[] = {
   {
      "7z", "application/x-7z-compressed\r\n"
   },
   {
      "css", "text/css\r\n"
   },
   {
      "htm", "text/html\r\n"
   },
   {
      "html", "text/html\r\n"
   },
   {
      "ico", "image/x-icon\r\n"
   },
   {
      "js", "application/javascript\r\n"
   },
   {
      "json", "application/json\r\n"
   },
   {
      "jpg", "image/jpeg\r\n"
   },
   {
      "mp3", "audio/mpeg\r\n"
   },
   {
      "ogg", "audio/ogg\r\n"
   },
   {
      "otf", "font/otf\r\n"
   },
   {
      "png", "image/png\r\n"
   },
   {
      "svg", "image/svg\r\n"
   },
   {
      "tar", "application/x-tar\r\n"
   },
   {
      "ttf", "font/ttf\r\n"
   },
   {
      "txt", "text/plain\r\n"
   },
   {
      "wasm", "application/wasm\r\n"
   },
   {
      "webp", "image/webp\r\n"
   },
   {
      "woff", "font/woff\r\n"
   },
   {
      "woff2", "font/woff2\r\n"
   },
   {
      "zip", "application/zip\r\n"
   },
   {
      NULL, NULL
   }
};

// Perform various checks on synthesized URLs to make sure the user isn't up to
// anything shady...
// XXX: Implement these!
bool check_url(const char *path) {
   return false;
}


// Returns HTTP Content-Type for the chosen short name (save some memory)
const char *http_content_type(const char *type) {
   if (!type) {
      return NULL;
   }
   int items = ( sizeof(http_res_types) / sizeof(struct http_res_types) );

   for (int i = 0 ; i <= items ; i++) {
//      printf("hct: %s, checking %d: %s\n",
//         type, i, http_res_types[i].shortname);
      // end of table marker?
      if (!http_res_types[i].shortname && !http_res_types[i].msg) {
         break;
      }

      // compare the short name
      if (strcasecmp(http_res_types[i].shortname, type) == 0) {
//         printf("hct: %s is [%d] => %s: %s\n",
//             type, i, http_res_types[i].shortname,
//             http_res_types[i].msg);
         return http_res_types[i].msg;
      }
   }

   return "text/plain\r\n";
}

#if     defined(USE_MONGOOSE)
bool http_static(struct mg_http_message *msg, struct mg_connection *c) {
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

   if ( file_exists(real_path) ) {
      // Find last '.' in the path for the extension
      const char *ext = strrchr(path, '.');

      if ( ext && *(ext + 1) ) {
         // lookup the mime type based on extension
         const char *ctype = http_content_type(ext + 1);
         char typebuf[256];
         // save it in a form mongoose likes
         memset( typebuf, 0, sizeof(typebuf) );
         snprintf(typebuf, sizeof(typebuf), "%s=%s", ext + 1, ctype);
         // tell mongoose about it
         opts.mime_types = ctype;
         // and serve the file
         mg_http_serve_dir(c, msg, &opts);

         return false;
      }
   } else if ( is_dir(real_path) ) {
      mg_http_serve_dir(c, msg, &opts);

      return false;
   } else {
      // file not found
      Log(LOG_DEBUG, "http.core", "Static dispatch for %s returning 404", path);
      mg_http_serve_file(c, msg, www_404_path, &opts);
   }

   return true;
}

///// Main HTTP callback
void ws_http_cb(struct mg_connection *c, int ev, void *ev_data) {
   if (!c) {
      return;
   }
   struct mg_http_message *hm = (struct mg_http_message *) ev_data;

   char ip[INET6_ADDRSTRLEN];   // Buffer to hold IPv4 or IPv6 address
   memset(ip, 0, INET6_ADDRSTRLEN);

   int port = c->rem.port;

   if (c->rem.is_ip6) {
      inet_ntop( AF_INET6, c->rem.addr.ip6, ip, sizeof(ip) );
   } else {
      inet_ntop( AF_INET, &c->rem.addr.ip4, ip, sizeof(ip) );
   }

   if (ev == MG_EV_OPEN) {
      if ( cfg_get_bool("net.http.hex-dump", false) ) {
         c->is_hexdumping = 1;
      }
   } else if (ev == MG_EV_CONNECT) {
      if (c->is_tls) {
         Log(LOG_DEBUG, "http", "Initializing TLS");
         struct mg_tls_opts opts;
         opts.ca = mg_str("*");
         mg_tls_init(c, &opts);
      }
   } else if (ev == MG_EV_ACCEPT) {
      Log(LOG_CRAZY, "http", "Accepted connection on mg_conn:<%p> from %s:%d", c, ip, port);

#if     defined(HTTP_USE_TLS)

      if (c->fn_data) {
         Log(LOG_CRAZY, "http", "Init TLS for mg_conn:<%p> from %s:%d", c, ip, port);
         mg_tls_init(c, &tls_opts);
      }
#endif
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

                  return;
               }
               memset(cptr->user_agent, 0, ua_len);
               memcpy(cptr->user_agent, ua_hdr->buf, ua_len);
               Log(LOG_DEBUG, "http.core", "New session c:<%p> cptr:<%p> User-Agent: %s (%d)", c, cptr,
                  (cptr->user_agent ? cptr->user_agent : "none"), ua_len);
            }
         }
      }

      // Send the request to our HTTP router
      if (hm && http_dispatch_route(hm, c) == true) {
         Log(LOG_CRAZY, "http.core", "fall through to http_static");
         http_static(hm, c);
      }
   } else if (ev == MG_EV_WS_OPEN) {
      Log(LOG_CRAZY, "http.core", "WS OPEN for c:<%p>", c);
      rrconn_t *cptr = http_find_client_by_c(c);

      if (cptr) {
         Log(LOG_DEBUG, "http", "Conn mg_conn:<%p> from %s:%d upgraded to ws with cptr:<%p>", c, ip, port, cptr);
         cptr->is_ws = true;
         char msgbuf[512];
         memset( msgbuf, 0, sizeof(msgbuf) );
         snprintf(msgbuf, sizeof(msgbuf),
           "{ \"hello\": {"
           "  \"swver\": \"rustyrig %s\","
           "  \"hwver\": \"%s\""
           "} }",
           VERSION, HARDWARE);
         mg_ws_send(c, msgbuf, strlen(msgbuf), WEBSOCKET_OP_TEXT);
      } else {
         Log(LOG_CRIT, "http", "Conn mg_conn:<%p> from %s:%d kicked: No cptr but tried to start ws", c, ip, port);
         ws_kick_client_by_c(c, "Socket error 314");
      }
   } else if (ev == MG_EV_WS_MSG) {
      struct mg_ws_message *msg = (struct mg_ws_message *)ev_data;
      ws_handle(msg, c);
   } else if (ev == MG_EV_CLOSE) {
      char resp_buf[HTTP_WS_MAX_MSG + 1];
      rrconn_t *cptr = http_find_client_by_c(c);
      const char *ip = cptr ? cptr->user_ip : "(unknown)";
      Log(LOG_DEBUG, "http", "http_cb MG_EV_CLOSE for cptr:<%p> c:<%p> ip:%s", cptr, c, ip);

      // make sure we're not accessing unsafe memory
      if (cptr && cptr->user && cptr->chatname[0] != '\0') {
         // Does the user hold PTT? if so turn it off
         if (cptr->is_ptt) {
            // XXX: This should only turn off PTT for the rig they are using!
//            rr_ptt_set_all_off();
            cptr->is_ptt = false;
            const char *jp = dict2json_mkstr(VAL_STR, "rig.ptt", "on", VAL_STR, "rig.ptt.user", cptr->chatname);
            event_emit("ptt", NULL, jp);
            free( (void *)jp );
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
         // reduce the # of clones for the user / reset to 0
         Log(LOG_CRAZY, "http", "Departing user %s had %d clones", cptr->chatname, cptr->user->clones);

         // We want to deal with clones
         if (cptr->user->clones < 0) {
            Log(LOG_CRIT, "http", "Likely bug in %s in %s:%d- cptr->user->clones < 1: %d", __FUNCTION__, __FILE__, __LINE__, cptr->user->clones);
         }

         if (cptr->active) {
            // blorp out a quit to all connected users
            const char *jp = dict2json_mkstr(VAL_STR, "talk.cmd", "quit", VAL_STR, "talk.user", cptr->chatname,
               VAL_ULONG, "talk.ts", now, VAL_STR, "talk.reason", "connection closed", VAL_INT, "talk.clones",
               cptr->user->clones, VAL_STR, "talk.ip", ip);
            struct mg_str ms = mg_str(jp);
            ws_broadcast(NULL, &ms, WEBSOCKET_OP_TEXT);
            free( (char *)jp );
            Log(LOG_AUDIT, "auth", "User %s on mg_conn:<%p> cptr:<%p> from %s:%d disconnected", cptr->chatname, c, cptr,
               ip, port);
         }
      } else {
         // This one makes a BUNCH of noise due to webui loading
         Log(LOG_CRAZY, "auth.extreme", "Unauthenticated client on mg_conn:<%p> from %s:%d disconnected", c, ip, port);
      }
      http_remove_client(c);
   }
}
#endif // defined(USE_MONGOOSE)

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
