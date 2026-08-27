//
// rrclient/ws.rigctl.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
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

extern time_t poll_block_expire, poll_block_delay;
extern time_t now;

// Store the previous mode
// XXX: this needs to go into the per-VFO
char old_mode[16];

bool ws_handle_rigctl_cli_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      Log(LOG_DEBUG, "ws.rigctl", "handle_rigctl_msg invalid args: cptr:<%p> d:<%p>", cptr, d);
      return true;
   }
   time_t ts = dict_get_time_t(d, "cat.ts", now);

   if (dict_get(d, "cat.state.mode", NULL) ) {
// XXX: Implement this - state message throttling & dict_diff usage
/*
      if (poll_block_expire < now) {
         return false;
      }
      poll_block_expire = now + poll_block_delay;
 */
      const char *vfo = dict_get(d, "cat.state.vfo", NULL);
      const char *mode = dict_get(d, "cat.state.mode", NULL);
      long freq = dict_get_long(d, "cat.state.freq", 0);
      int width = dict_get_int(d, "cat.state.width", 0);
      int power = dict_get_int(d, "cat.state.power", 0);
      bool ptt = dict_get_bool(d, "cat.state.ptt", false);
      int ts = dict_get_int(d, "cat.ts", 0);
      const char *user = dict_get(d, "cat.user", NULL);

      if (user && *user) {
         Log(LOG_DEBUG, "ws.cat", "user:<%p> = |%s|", user, user);
         struct rr_user *cptr = NULL;
#if	0
         if ((cptr = userlist_find(user))) {
            Log(LOG_DEBUG, "ws.cat", "ptt set to %s for cptr:<%p>",
              (cptr->is_ptt ? "true" : "false"), cptr);
            cptr->is_ptt = ptt;
         }
#endif

         char real_mode[32];
         memset(real_mode, 0, sizeof(real_mode));
         snprintf(real_mode, sizeof(real_mode), "%s", mode);

         if (real_mode && strlen(real_mode) > 0) {
            // XXX: We need to suppress sending a CAT message by disabling the
            // changed signal on the mode combo
            if (strcasecmp(real_mode, "PKTUSB") == 0) {
               memset(real_mode, 0, 6);
               sprintf(real_mode, "D-U");
            } else if (strcasecmp(real_mode, "PKTLSB") == 0) {
               memset(real_mode, 0, 6);
               sprintf(real_mode, "D-L");
            }

            if (strcasecmp(old_mode, real_mode) == 0) {
               goto local_cleanup;
            }
// XXX: need to fix FM mode dialog crash ASAP
            Log(LOG_CRAZY, "ws.rigctl", "Set MODE to %s", real_mode);

/*
 *           if (strcasecmp(real_mode, "FM") == 0) {
 *  //               fm_dialog_show();
 *           } else {
 *              // Hide the FM dialog
 *  //               fm_dialog_hide();
 *           }
 */
            // save the old mode so we can compare next time
            memset( old_mode, 0, sizeof(old_mode) );
            snprintf(old_mode, sizeof(old_mode), "%s", real_mode);
         }
      }
   } else {
      char *json_msg = dict2json(d);
//      ui_print("[%s] ==> CAT: Unknown msg -- %s", get_chat_ts(ts), json_msg);
      Log(LOG_DEBUG, "ws.cat", "Unknown msg: %s", json_msg);
      free(json_msg);
   }

local_cleanup:
   return false;
}

bool ws_send_ptt_cmd(rrconn_t *cptr, const char *vfo, bool ptt) {
   if (!cptr || !vfo) {
      return true;
   }
   dict *cat_msg = dict_new();
   dict_add(cat_msg, "cat.cmd", "ptt");
   dict_add(cat_msg, "cat.vfo", vfo);
   dict_add_bool(cat_msg, "cat.ptt", ptt);
   dict_add_ulong(cat_msg, "cat.ts", now);
   ws_send_dict(NULL, cptr, cat_msg, WEBSOCKET_OP_TEXT);
   dict_free(cat_msg);

   return false;
}

bool ws_send_mode_cmd(rrconn_t *cptr, const char *vfo, const char *mode) {
   if (!cptr || !vfo || !mode) {
      return true;
   }
   dict *cat_msg = dict_new();
   dict_add(cat_msg, "cat.cmd", "mode");
   dict_add(cat_msg, "cat.vfo", vfo);
   dict_add(cat_msg, "cat.mode", mode);
   dict_add_ulong(cat_msg, "cat.ts", now);
   ws_send_dict(NULL, cptr, cat_msg, WEBSOCKET_OP_TEXT);
   dict_free(cat_msg);

   return false;
}

bool ws_send_freq_cmd(rrconn_t *cptr, const char *vfo, long freq) {
   if (!cptr || !vfo) {
      return true;
   }
   dict *cat_msg = dict_new();
   dict_add(cat_msg, "cat.cmd", "freq");
   dict_add(cat_msg, "cat.vfo", vfo);
   dict_add_long(cat_msg, "cat.freq", freq);
   dict_add_ulong(cat_msg, "cat.ts", now);
   ws_send_dict(NULL, cptr, cat_msg, WEBSOCKET_OP_TEXT);
   dict_free(cat_msg);

   return false;
}
