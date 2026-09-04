// rrclient/ws.chat.c
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

extern time_t now;		// main.c

bool ws_handle_talk_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_DEBUG, "ws.chat", "handle_talk_msg: cptr:<%p> d:<%p>", cptr, d);
      return true;
   }
   const char *cmd = dict_get(d, "talk.cmd", NULL);
   const char *user = dict_get(d, "talk.user", NULL);
   const char *privs = dict_get(d, "talk.privs", NULL);
   const char *muted = dict_get(d, "talk.muted", NULL);
   const char *ts = dict_get(d, "msg.ts", NULL);
   int clones = dict_get_int(d, "talk.clones", 11);
   bool rv = false;
   bool tx = dict_get_bool(d, "talk.state.tx", false);

   if (!cmd) {
      rv = true;
      return true;
   }

   if (cmd && strcasecmp(cmd, "userinfo") == 0) {
      if (!user) {
         rv = true;
         return true;
      }
      Log(LOG_DEBUG, "ws.talk", "UserInfo: %s has privs '%s' (TX: %s, Muted: %s, clones: %d)", user, privs,
         (tx ? "true" : "false"), (muted ? "true" : "false"), clones);
      event_emit_dict("userinfo", cptr, d);
   } else if (cmd && strcasecmp(cmd, "msg") == 0) {
      const char *from = dict_get(d, "talk.from", NULL);
      const char *data = dict_get(d, "talk.data", NULL);
      const char *msg_type = dict_get(d, "talk.msg_type", NULL);
      const char *target = dict_get(d, "talk.target", NULL);
      time_t ts = dict_get_time_t(d, "msg.ts", now);

      if (from) {
         event_emit_dict("talk.msg", cptr, d);
      }
   } else if (cmd && strcasecmp(cmd, "join") == 0) {
      if (!user) {
         return true;
      }
      event_emit_dict("join", cptr, d);
   } else if (cmd && strcasecmp(cmd, "quit") == 0) {
      if (!user) {
         return true;
      }
      char *quit_user = strdup(user);

      if (!quit_user) {
         return true;
      }
      Log(LOG_INFO, "ws.chat", "talk: sending quit for %s", quit_user);
      event_emit_dict("quit", cptr, d);
      free(quit_user);
   } else if (cmd && strcasecmp(cmd, "whois") == 0) {
      const char *whois_msg = dict_get(d, "talk.data", NULL);
      event_emit_dict("whois", cptr, d);
   }
   return false;
}
