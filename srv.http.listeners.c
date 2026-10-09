// srv.http.listeners.c: Setup our websocket (http) and wss (ws+tls) listeners
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

// In srv.http.c
#ifdef  USE_MONGOOSE
extern void ws_http_cb(struct mg_connection *c, int ev, void *ev_data);
#endif // USE_MONGOOSE

// This defines a hard-coded fallback path for httpd root, if not set in config
#ifdef  HOST_POSIX
#ifndef INSTALL_PREFIX
#define WWW_ROOT_FALLBACK "./www"
#define WWW_404_FALLBACK "./www/404.html"
#endif // !INSTALL_PREFIX
#else
#define WWW_ROOT_FALLBACK "fs:www/"
#define WWW_404_FALLBACK "fs:www/404.html"
#endif // HOST_POSIX.else

extern char www_root[PATH_MAX];
extern char www_fw_ver[128];
extern char www_headers[32768];
extern char www_404_path[PATH_MAX];
extern rrconn_t *http_client_list;

/* Configuration values may come from the normal dictionary or EEPROM.  Keep ownership local and expand ~/$HOME before checking installed/development paths. */
static char *http_config_path(const char *key, const char *eeprom_key) {
   char *raw = (char *)cfg_get_exp(key);

#ifdef USE_EEPROM

   if (!raw && eeprom_key) {
      const char *eeprom_path = eeprom_get_str(eeprom_key);

      if (eeprom_path) {
         raw = strdup(eeprom_path);
      }
   }
#else
   (void)eeprom_key;
#endif

   if (!raw) {
      return NULL;
   }

   char *expanded = expand_path(raw);
   free(raw);

   return expanded;
}

#ifdef  USE_MONGOOSE
extern struct mg_mgr mg_mgr;

#ifdef  HTTP_USE_TLS
struct mg_str tls_cert;
struct mg_str tls_key;

struct mg_tls_opts tls_opts;

void http_tls_init(void) {
   bool tls_error = false;
   memset(&tls_opts, 0, sizeof(tls_opts) );

   tls_cert = mg_file_read(&mg_fs_posix, HTTP_TLS_CERT);

   if (!tls_cert.buf) {
      Log(LOG_CRIT, "http.tls", "Unable to load TLS cert from %s", HTTP_TLS_CERT);
      tls_error = true;
   }
   tls_key = mg_file_read(&mg_fs_posix, HTTP_TLS_KEY);

   if (!tls_key.buf || tls_key.len <= 1) {
      Log(LOG_CRIT, "http.tls", "Unable to load TLS key from %s", HTTP_TLS_KEY);
      tls_error = true;
   }

   if (tls_error == true) {
      Log(LOG_CRIT, "http.tls", "No cert/key, aborting TLS setup");
      Log(LOG_CRIT, "http.tls", "Either fix this or disable TLS!");
      exit(1);
   } else {
      tls_opts.cert = tls_cert;
      tls_opts.key = tls_key;
      tls_opts.skip_verification = 1;
      Log(LOG_INFO, "http.tls", "TLS initialized succesfully, |cert: <%lu @ %p>| |key: <%lu @ %p>", tls_cert.len, tls_cert, tls_key.len, tls_key.buf);
   }
}
#endif // HTTP_USE_TLS
#endif // USE_MONGOOSE

bool http_init(struct mg_mgr *mgr) {
   if (!mgr) {
      Log(LOG_CRIT, "http", "http_init passed NULL mgr!");

      return true;
   }
   char *cfg_www_root = http_config_path("net.http.www-root", "net/http/www-root");
   char *cfg_404_path = http_config_path("net.http.404-path", "net/http/404-path");

#if     0 // XXX: fix this
   // store firmware version in www_fw_ver
   prepare_msg(www_fw_ver, sizeof(www_fw_ver), "X-Version: rustyrig %s on %s", VERSION, HARDWARE);

   // and make our headers
   prepare_msg(www_headers, sizeof(www_headers), "%s\r\n", www_fw_ver);
#endif // 0

   // Use a configured root when it exists. A package config can remain
   // installed while the daemon is run from a source tree, so fall back to
   // the current directory (and the normal package state directory) if the
   // configured path is unavailable.
   const char *selected_root = NULL;

   if (cfg_www_root && is_dir(cfg_www_root) ) {
      selected_root = cfg_www_root;
   }
#ifdef HOST_POSIX

   if (!selected_root) {
      const char *fallbacks[] = {
         WWW_ROOT_FALLBACK, "/var/lib/rustyrig/www",
         "./share/rustyrig/www", NULL
      };

      for (int i = 0 ; fallbacks[i] ; i++) {
         if (is_dir(fallbacks[i]) ) {
            selected_root = fallbacks[i];
            Log(LOG_INFO, "http.init", "Configured www-root unavailable; using %s", selected_root);
            break;
         }
      }
   }
#endif
   prepare_msg(www_root, sizeof(www_root), "%s", selected_root ? selected_root : (cfg_www_root ? cfg_www_root : WWW_ROOT_FALLBACK) );
   Log(LOG_INFO, "http.init", "Set www-root to %s", www_root);

   // Prefer an existing configured 404 page, then the selected root's page,
   // and finally the platform fallback.
   if (cfg_404_path && is_file(cfg_404_path) ) {
      prepare_msg(www_404_path, sizeof(www_404_path), "%s", cfg_404_path);
   } else {
      char root_404[PATH_MAX];
      snprintf(root_404, sizeof(root_404), "%s/404.html", www_root);

      if (is_file(root_404) ) {
         prepare_msg(www_404_path, sizeof(www_404_path), "%s", root_404);
      } else {
         prepare_msg(www_404_path, sizeof(www_404_path), "%s", WWW_404_FALLBACK);
      }
   }
   free(cfg_404_path);
   free(cfg_www_root);

   int user_count = http_reload_users();

   if (user_count < 0) {
      Log(LOG_WARN, "http.core", "Error loading users from authdb");
   }

   // Reload the user database whenever net.http.authdb* changes (rehash etc)
   reload_event_add("net.http.authdb", http_reload_users_cb, "Reload HTTP users from authdb");
   reload_event_add("net.http.authdb-dynamic", http_reload_users_cb, "Reload HTTP users from authdb");
   struct in_addr sa_bind = {
      0
   };
   sa_bind.s_addr = htonl(INADDR_ANY);
   char listen_addr[255];
   int bind_port = cfg_get_int("net.http.port", 8420);

#ifdef  USE_EEPROM

   if (!bind_port) {
      bind_port = eeprom_get_int("net/http/port");
   }
#endif // USE_EEPROM

   const char *s = cfg_get("net.http.bind");

   if (!s || !inet_aton(s, &sa_bind) ) {
#ifdef  USE_EEPROM
      eeprom_get_ip4("net/http/bind", &sa_bind);
#endif // USE_EEPROM
   }
   free( (char *)s);
   prepare_msg(listen_addr, sizeof(listen_addr), "http://%s:%d", inet_ntoa(sa_bind), bind_port);

#ifdef  USE_MONGOOSE
   fprintf(stderr, "mgr: <%p>, listen_addr:<%p> = %s\n", mgr, listen_addr, listen_addr);

   if (!mg_http_listen(mgr, listen_addr, ws_http_cb, NULL) ) {
      Log(LOG_CRIT, "http", "Failed to start http listener -- is program already running or something else listening on port %d?", bind_port);

      // If net.http.required is set, exit cleanly rather than limping along
      if (cfg_get_bool("net.http.required", false) ) {
         Log(LOG_CRIT, "http", "net.http.required is set, exiting");
         exit(EXIT_FAILURE);
      }

      Log(LOG_CRIT, "http", "Continuing without http listener (net.http.required is false)");
   }

   Log(LOG_INFO, "http", "HTTP listening at %s with www-root at %s", listen_addr, www_root);

#ifdef  HTTP_USE_TLS

   if (cfg_get_bool("net.http.tls-enabled", false) ) {
      int tls_bind_port = cfg_get_int("net.http.tls-port", 8443);

#ifdef  USE_EEPROM

      if (!tls_bind_port) {
         tls_bind_port = eeprom_get_int("net/http/tls_port");
      }
#endif // USE_EEPROM

      struct in_addr sa_tls_bind = {
         0
      };
      sa_tls_bind.s_addr = htonl(INADDR_ANY);
      s = cfg_get_exp("net.http.tls-bind");

      if (!s || !inet_aton(s, &sa_tls_bind) ) {
#ifdef  USE_EEPROM
         eeprom_get_ip4("net/http/bind", &sa_tls_bind);
#endif // USE_EEPROM
      }
      free( (char *)s);
      s = NULL;

      char tls_listen_addr[255];
      prepare_msg(tls_listen_addr, sizeof(tls_listen_addr), "https://%s:%d", inet_ntoa(sa_tls_bind), tls_bind_port);
      http_tls_init();

      if (!mg_http_listen(mgr, tls_listen_addr, ws_http_cb, NULL) ) {
         Log(LOG_CRIT, "http", "Failed to start https listener -- is program already running or something else listening on port %d?", tls_bind_port);

         // If net.http.required is set, exit cleanly rather than limping along
         if (cfg_get_bool("net.http.required", false) ) {
            Log(LOG_CRIT, "http", "net.http.required is set, exiting");
            exit(EXIT_FAILURE);
         }

         Log(LOG_CRIT, "http", "Continuing without https listener (net.http.required is false)");
      }
      Log(LOG_INFO, "http", "HTTPS listening at %s with www-root at %s", tls_listen_addr, www_root);
   }
#endif // HTTP_USE_TLS
#endif // USE_MONGOOSE

   return false;
}
