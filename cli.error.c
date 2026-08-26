//
// librrprotocol/cli.error.c: Client error handling
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

extern time_t now;

bool ws_handle_error_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_WARN, "http.ws", "error_msg: got cptr:<%p> d:<%p>", cptr, d);
      return true;
   }
   bool rv = false;

   char ip[INET6_ADDRSTRLEN];
   int port = 0;

#ifdef	USE_MONGOOSE
   port = cptr->conn->rem.port;
   if (cptr->conn->rem.is_ip6) {
      inet_ntop( AF_INET6, cptr->conn->rem.addr.ip6, ip, sizeof(ip) );
   } else {
      inet_ntop( AF_INET, &cptr->conn->rem.addr.ip4, ip, sizeof(ip) );
   }
#endif	// USE_MONGOOSE
   char *error_msg = dict_get(d, "error.msg", NULL);
   char *error_from = dict_get(d, "error.from", NULL);
   time_t ts = dict_get_time_t(d, "error.ts", now);

   if (!error_from) {
      dict_add(d, "error.from", "***SERVER***");
   }
   event_emit_dict("error", NULL, d);
   return false;
}
