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

bool ws_handle_ping_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_WARN, "http.ws", "ping_msg: got d:<%p> cptr:<%p>", d, cptr);
      return true;
   }
   bool rv = false;

   char *ip = cptr->user_ip;
   int port = cptr->user_port;
   time_t ping_ts = dict_get_time_t(d, "msg.ts", 0);

   if (ping_ts) {
      dict *pong_msg = dict_new();
      dict_add(pong_msg, "msg.type", "pong");
      dict_add_ulong(pong_msg, "msg.ts", ping_ts);
      // Echo the server's monotonic ping.ts (real microseconds) back so it can measure RTT
      long long mono_ts = dict_get_llong(d, "ping.ts", 0);
      if (mono_ts) {
         dict_add_llong(pong_msg, "ping.ts", mono_ts);
      }
      ws_send_dict(NULL, cptr, pong_msg, WEBSOCKET_OP_TEXT);
      dict_free(pong_msg);
   } else {
      Log(LOG_WARN, "ws.ping", "*** Empty ping?? ***");
   }

   if (cfg_show_pings) {
      Log(LOG_CRAZY, "ws.ping", "* Ping? Pong! %lld *", ping_ts);
   }

   return false;
}

// Handle a PONG reply from the server: log RTT like webui.latencycalc.js does
bool ws_handle_pong_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_WARN, "http.ws", "pong_msg: got d:<%p> cptr:<%p>", d, cptr);
      return true;
   }

   time_t pong_ts = dict_get_time_t(d, "msg.ts", 0);
   if (!pong_ts) {
      Log(LOG_WARN, "ws.pong", "PONG with no timestamp from server");
      return true;
   }

   time_t now = time(NULL);
   Log(LOG_CRAZY, "ws.pong", "* Pong! RTT: %lld secs *", (long long)(now - pong_ts));

   return false;
}
