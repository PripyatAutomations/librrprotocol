//
// rrclient/ws.notice.c
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

bool ws_handle_notice_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_WARN, "http.ws", "notice_msg: got cptr:<%p> d:<%p>", cptr, d);
      return true;
   }
   bool rv = false;
   char *ip = cptr->user_ip;
   int port = cptr->user_port;
   const char *notice_msg = dict_get(d, "talk.msg", NULL);
   const char *notice_from = dict_get(d, "talk.from", NULL);
   time_t ts = dict_get_time_t(d, "talk.ts", now);

   event_emit_dict("talk.msg", NULL, d);
   return false;
}
