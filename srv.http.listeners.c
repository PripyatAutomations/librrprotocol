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
#ifdef	HOST_POSIX
#ifndef	INSTALL_PREFIX
#define	WWW_ROOT_FALLBACK "./www"
#define	WWW_404_FALLBACK "./www/404.html"
#endif // !INSTALL_PREFIX
#else
#define	WWW_ROOT_FALLBACK "fs:www/"
#define	WWW_404_FALLBACK "fs:www/404.html"
#endif // HOST_POSIX.else

extern char www_root[PATH_MAX];
extern char www_fw_ver[128];
extern char www_headers[32768];
extern char www_404_path[PATH_MAX];
extern rrconn_t *http_client_list;

#ifdef	USE_MONGOOSE
extern struct mg_mgr mg_mgr;

#ifdef	HTTP_USE_TLS
struct mg_str tls_cert;
struct mg_str tls_key;

struct mg_tls_opts tls_opts;

void http_tls_init(void) {
   bool tls_error = false;
   memset( &tls_opts, 0, sizeof(tls_opts) );

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
      Log(LOG_INFO, "http.tls", "TLS initialized succesfully, |cert: <%lu @ %p>| |key: <%lu @ %p>", tls_cert.len,
         tls_cert, tls_key.len, tls_key.buf);
   }
}
#endif // HTTP_USE_TLS
#endif // USE_MONGOOSE

bool http_init(struct mg_mgr *mgr) {
   if (!mgr) {
      Log(LOG_CRIT, "http", "http_init passed NULL mgr!");

      return true;
   }
   const char *cfg_www_root = cfg_get_exp("net.http.www-root");
   const char *cfg_404_path = cfg_get_exp("net.http.404-path");

#ifdef	USE_EEPROM
   if (!cfg_www_root) {
      cfg_www_root = eeprom_get_str("net/http/www-root");
   }

   if (!cfg_404_path) {
      cfg_404_path = eeprom_get_str("net/http/404-path");
   }
#endif	// USE_EEPROM

#if     0 // XXX: fix this
   // store firmware version in www_fw_ver
   prepare_msg(www_fw_ver, sizeof(www_fw_ver), "X-Version: rustyrig %s on %s", VERSION, HARDWARE);

   // and make our headers
   prepare_msg(www_headers, sizeof(www_headers), "%s\r\n", www_fw_ver);
#endif	// 0

   // store the 404 path if available
   if (cfg_404_path) {
      prepare_msg(www_404_path, sizeof(www_404_path), "%s", WWW_404_FALLBACK);
   } else {
      prepare_msg(www_404_path, sizeof(www_404_path), "%s", WWW_404_FALLBACK);
   }
   free( (char *)cfg_404_path );
   cfg_404_path = NULL;

   // set the www-root if configured
   if (cfg_www_root) {
      prepare_msg(www_root, sizeof(www_root), "%s", cfg_www_root);
   } else {
      // use the defaults
      prepare_msg(www_root, sizeof(www_root), "%s", WWW_ROOT_FALLBACK);
   }
   Log(LOG_CRIT, "http.init", "set www-root to %s", www_root);
   free( (char *)cfg_www_root );
   cfg_www_root = NULL;

   if (http_load_users(HTTP_AUTHDB_PATH) < 0) {
      Log(LOG_WARN, "http.core", "Error loading users from %s", HTTP_AUTHDB_PATH);
   }
   struct in_addr sa_bind;
   char listen_addr[255];
   int bind_port = cfg_get_int("net.http.port", 0);

#ifdef	USE_EEPROM
   if (!bind_port) {
      bind_port = eeprom_get_int("net/http/port");
   }
#endif	// USE_EEPROM

   const char *s = cfg_get("net.http.bind");

   if (!s || !inet_aton(s, &sa_bind) ) {
#ifdef	USE_EEPROM
      eeprom_get_ip4("net/http/bind", &sa_bind);
#endif	// USE_EEPROM
   }
   free( (char *)s );
   prepare_msg(listen_addr, sizeof(listen_addr), "http://%s:%d", inet_ntoa(sa_bind), bind_port);

#ifdef  USE_MONGOOSE
   fprintf(stderr, "mgr: <%p>, listen_addr:<%p> = %s\n", mgr, listen_addr, listen_addr);

   if (!mg_http_listen(mgr, listen_addr, ws_http_cb, NULL) ) {
      Log(LOG_CRIT, "http", "Failed to start http listener -- is program already running or something else listening on port %d?", bind_port);
      exit(1);
   }

   Log( LOG_INFO, "http", "HTTP listening at %s with www-root at %s", listen_addr,
      (cfg_www_root ? cfg_www_root : WWW_ROOT_FALLBACK) );

#ifdef	HTTP_USE_TLS
   if (cfg_get_bool("net.http.tls-enabled", false) ) {
      int tls_bind_port = cfg_get_int("net.http.tls-port", 0);

#ifdef	USE_EEPROM
      if (!tls_bind_port) {
         tls_bind_port = eeprom_get_int("net/http/tls_port");
      }
#endif	// USE_EEPROM

      struct in_addr sa_tls_bind;
      s = cfg_get_exp("net.http.tls-bind");

      if (!s || !inet_aton(s, &sa_tls_bind) ) {
#ifdef	USE_EEPROM
         eeprom_get_ip4("net/http/bind", &sa_tls_bind);
#endif	// USE_EEPROM
      }
      free( (char *)s );
      s = NULL;

      char tls_listen_addr[255];
      prepare_msg(tls_listen_addr, sizeof(tls_listen_addr), "https://%s:%d", inet_ntoa(sa_tls_bind), tls_bind_port);
      http_tls_init();

      if (!mg_http_listen(mgr, tls_listen_addr, ws_http_cb, NULL) ) {
         Log(LOG_CRIT, "http", "Failed to start https listener -- is program already running or something else listening on port %d?",
            tls_bind_port);
         exit(1);
      }
      Log( LOG_INFO, "http", "HTTPS listening at %s with www-root at %s", tls_listen_addr,
         (cfg_www_root ? cfg_www_root : WWW_ROOT_FALLBACK) );
   }
#endif // HTTP_USE_TLS
#endif // USE_MONGOOSE
   return false;
}
