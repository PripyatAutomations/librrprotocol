//
// rrclient/ws.syslog.c
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
extern time_t now;

bool ws_handle_syslog_msg(rrconn_t *cptr, dict *d) {
   bool rv = false;

   if (!cptr || !d) {
      Log(LOG_WARN, "http.ws", "syslog_msg: got cptr:<%p> d:<%p>", cptr, d);
      return true;
   }
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
   char *ts = dict_get(d, "syslog.ts", NULL);
   char *prio = dict_get(d, "syslog.prio", NULL);
   char *subsys = dict_get(d, "syslog.subsys", NULL);
   char *data = dict_get(d, "syslog.data", NULL);
   char my_timestamp[64];
   time_t t;
   struct tm *tmp;
   memset( my_timestamp, 0, sizeof(my_timestamp) );
   t = time(NULL);

   if ( (tmp = localtime(&t) ) ) {
      // success, proceed
      if (strftime(my_timestamp, sizeof(my_timestamp), "%Y/%m/%d %H:%M:%S", tmp) == 0) {
         // if strftime fails: handle the error by printing the time_t
         memset( my_timestamp, 0, sizeof(my_timestamp) );
         snprintf( my_timestamp, sizeof(my_timestamp), "<%ld>", (long)time(NULL) );
      }
   }

// XXX: This needs some testing to make sure its robust
//   logpriority_t log_priority = log_priority_from_str(prio);
   Log(LOG_DEBUG, "server.syslog", "remote syslog: [%s] <%s.%s> %s", my_timestamp, subsys, prio, data);
   return false;
}
