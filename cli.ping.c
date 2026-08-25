//
// rrclient/ws.ping.c
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

extern dict *cfg;                // config.c
extern bool cfg_show_pings;

#ifdef	USE_MONGOOSE
bool ws_handle_ping_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_WARN, "http.ws", "ping_msg: got d:<%p> cptr:<%p>", d, cptr);
      return true;
   }
   bool rv = false;

   char ip[INET6_ADDRSTRLEN];
   int port = cptr->conn->rem.port;

   if (cptr->conn->rem.is_ip6) {
      inet_ntop( AF_INET6, cptr->conn->rem.addr.ip6, ip, sizeof(ip) );
   } else {
      inet_ntop( AF_INET, &cptr->conn->rem.addr.ip4, ip, sizeof(ip) );
   }
   time_t ping_ts = dict_get_time_t(d, "ping.ts", 0);

   if (ping_ts) {
      dict *d = dict_new();
      dict_add_ulong(d, "pong.ts", ping_ts);
      ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
      dict_free(d);
   } else {
      Log(LOG_WARN, "ws.ping", "*** Empty ping?? ***");
   }

   if (cfg_show_pings) {
      Log(LOG_DEBUG, "ws.ping", "* Ping? Pong! %lld *", ping_ts);
   }

   return false;
}

#endif // USE_MONGOOSE
