//
// librrprotoco;/srv.rigctl.c: Rig control stuff on the server side
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
#include <rrserver/backend.h>

bool rr_set_width(rr_vfo_t vfo, const char *width) {
   return false;
}

bool rr_set_mode(rr_vfo_t vfo, rr_mode_t mode) {
   return false;
}

extern time_t now;

time_t cfg_backed_poll_interval = 60;

// TODO: Merge with existing rr_vfo_data_t
typedef struct ws_rig_state {
   long freq;
   rr_mode_t mode;
   int width;
} ws_rig_state_t;

// Here we keep track of a few sets of VFO state
static ws_rig_state_t vfo_states[MAX_VFOS], vfo_states_last[MAX_VFOS];
time_t ws_rig_state_last_sent;

ws_rig_state_t *ws_rig_get_vfo_state(rr_vfo_t vfo) {
   return &vfo_states[vfo];
}

ws_rig_state_t *ws_rig_get_vfo_last_state(rr_vfo_t vfo) {
   return &vfo_states_last[vfo];
}

// Returns NULL or a diff of the last and current rig statef
static ws_rig_state_t *ws_rigctl_state_diff(rr_vfo_t vfo) {
   if (vfo == VFO_NONE) {
      return NULL;
   }
   // shortcut pointers
   ws_rig_state_t *curr = &vfo_states[vfo],
                  *old = &vfo_states_last[vfo];

   // allocate some storage for the diff values
   ws_rig_state_t *update = malloc( sizeof(ws_rig_state_t) );

   if (!update) {
      fprintf(stderr, "OOM in ws_rigctl_state_diff!\n");
      abort();
   }
   memset( update, 0, sizeof(ws_rig_state_t) );

   bool u_freq = false, u_mode = false, u_width = false;

   // Only propogate changed fields
   if (old->freq != curr->freq) {
      update->freq = curr->freq;
      u_freq = true;
   }

   if (old->mode != curr->mode) {
      update->mode = curr->mode;
      u_mode = true;
   }

   if (old->width != curr->width) {
      update->width = curr->width;
      u_width = true;
   }

   if (u_freq || u_mode || u_width) {
      return update;
   }
   // If no changes, return NULL
   free( (void *)update);

   return NULL;
}

// Save the old state then poll the rig
static bool ws_rig_state_poll(rr_vfo_t vfo) {
   // shortcut pointers
   ws_rig_state_t *curr = &vfo_states[vfo],
                  *old = &vfo_states_last[vfo];

   // save the current values to _last
   memset( old, 0, sizeof(ws_rig_state_t) );
   memcpy( old, curr, sizeof(ws_rig_state_t) );

#if     0
   // Poll the backend
   if (rig.backend && rig.backend->api && rig.backend->api->backend_poll) {
      rig.backend->api->backend_poll();
   }
#endif
   return false;
}

// Sends a diff of the changes since last poll, in json
static bool ws_rig_state_send(rr_vfo_t vfo) {
   bool force_send = false;

   if (vfo == VFO_NONE) {
      return NULL;
   }

   // Nothing to return, see if we've iterated enough times to force a send
   if (ws_rig_state_last_sent >= cfg_backed_poll_interval) {
      force_send = true;
   }
   ws_rig_state_t *diff = NULL;

   if (force_send) {
      // send the entire latest update to the users
      diff = &vfo_states[vfo];
   } else {
      ws_rigctl_state_diff(vfo);

      if (!diff) {
         return false;
      }
   }
   // update last sent and return success
   ws_rig_state_last_sent = now;
   return false;
}

/*
 time_t cfg_backed_poll_interval = 60;
 cfg_backed_poll_interval = cfg_get_int("backend.poll-interval", 60);
*/
bool ws_handle_rigctl_msg(rrconn_t *cptr, dict *d) {
   bool rv = false;

   if (!cptr) {
      return true;
   }
   cptr->last_heard = now;       // avoid unneeded keep-alives
   cptr->last_cat = now;         // last CAT message received from user
   const char *cmd = dict_get(d, "cat.cmd", NULL);
   // Accept both the nested client format (cat.vfo) and the state format (cat.state.vfo)
   const char *vfo = dict_get(d, "cat.vfo", NULL);
   if (!vfo) vfo = dict_get(d, "cat.state.vfo", NULL);
   const char *state = dict_get(d, "cat.state", NULL);

   if (cptr->user->is_muted) {
      Log(LOG_AUDIT, "ws.rigctl", "Ignoring %s command from %s as they are muted!", cmd, cptr->chatname);
      // XXX: Inform the user they are muted and can't use rigctl
      dict *d_err = dict_new();
      dict_add(d_err, "msg.type", "error");
      dict_add(d_err, "error.msg", "Invalid target");
      dict_add(d_err, "error.vfo", vfo);
      dict_add(d_err, "error.target", cptr->chatname);
      ws_send_dict(NULL, cptr, d_err, WEBSOCKET_OP_TEXT);
      dict_free(d_err);
      return true;
   }

   // Support for 'noob' class users who can only control rig if an elmer is
   // present
   // XXX: Add support for per noob Elmer (link from noob to elmer(s) who have
   // approved their use)
   if (client_has_flag(cptr, FLAG_NOOB) && !is_elmer_online() ) {
      Log(LOG_AUDIT, "ws.rigctl", "Ignoring %s command from %s as they're a noob and no elmers are online", cmd,
         cptr->chatname);
      return true;
   }

   if (cmd) {
      if (strcasecmp(cmd, "ptt") == 0) {
         if (!has_priv(cptr->user->uid, "admin|owner|tx|noob") || cptr->user->is_muted) {
            return true;
         }

         if (!vfo) {
            Log(LOG_DEBUG, "ws.rigctl", "PTT set without vfo or ptt_state");
            return true;
         }
         bool ptt_state = dict_get_bool(d, "cat.state.ptt", false);
         rr_vfo_t c_vfo = vfo_lookup(vfo[0]);

         // Gather some data about the VFO
         rr_vfo_t vfo_id = VFO_NONE;
         const char *mode_name = NULL;

         vfo_id = vfo_lookup(vfo[0]);

         if (vfo_id < 0) {
            return true;
         }
         rr_vfo_data_t *dp = &vfos[vfo_id];
         mode_name = vfo_mode_name(dp->mode);

         int channel = -1;

         // XXX: We need to look up the channel ID for RX *FROM* the client
         if (channel < 0) {
            Log(LOG_CRIT, "ptt", "Couldn't find channel ID for TX stream, ignoring PTT event");
            return true;
            // XXX: send an error & ptt off notice
         }

         // Update their last heard and PTT status
         cptr->last_heard = now;
         cptr->last_cat = now;         // last CAT message received from user
         cptr->is_ptt = ptt_state;

         // Send to log file & consoles
         Log(LOG_AUDIT, "ptt", "User %s set PTT to %s on vfo %s", cptr->chatname, (ptt_state ? "true" : "false"), vfo);
         dict *cat_msg = dict_new();
         dict_add(cat_msg, "msg.type", "cat");
         dict_add(cat_msg, "cat.cmd", "ptt");
         dict_add(cat_msg, "cat.mode", mode_name);
         dict_add_bool(cat_msg, "cat.ptt", ptt_state);
         dict_add(cat_msg, "cat.user", cptr->chatname);
         dict_add(cat_msg, "cat.vfo", vfo);
         dict_add_float(cat_msg, "cat.power", dp->power);
         dict_add_long(cat_msg, "cat.freq", dp->freq);
         dict_add_int(cat_msg, "cat.width", dp->width);
         dict_add_ulong(cat_msg, "msg.ts", now);
         ws_broadcast_dict(NULL, cat_msg, WEBSOCKET_OP_TEXT);

         // Send a PTT event
         event_emit_dict("ptt", NULL, cat_msg);
         dict_free(cat_msg);
      } else if (strcasecmp(cmd, "freq") == 0) {
         if (!has_priv(cptr->user->uid, "admin|owner|tx|noob") || cptr->user->is_muted) {
            return true;
         }
         long new_freq = dict_get_long(d, "cat.state.freq", 0);
         if (new_freq <= 0) new_freq = dict_get_long(d, "cat.freq", 0);

         if (!vfo || new_freq <= 0) {
            Log(LOG_DEBUG, "ws.rigctl", "FREQ set without vfo or freq");
            return true;
         }

         rr_vfo_t c_vfo;
         c_vfo = vfo_lookup(vfo[0]);
         cptr->last_cat = now;         // last CAT message received from user
         cptr->last_heard = now;

         // tell everyone about it
         dict *cat_msg = dict_new();
         dict_add(cat_msg, "msg.type", "cat");
         dict_add(cat_msg, "cat.cmd", "freq");
         dict_add_long(cat_msg, "cat.freq", new_freq);
         dict_add_ulong(cat_msg, "msg.ts", now);
         dict_add(cat_msg, "cat.user", cptr->chatname);
         dict_add(cat_msg, "cat.vfo", vfo);

         ws_broadcast_dict(NULL, cat_msg, WEBSOCKET_OP_TEXT);
         Log(LOG_AUDIT, "ws.cat", "User %s set VFO %s FREQ to %d hz", cptr->chatname, vfo, new_freq);
         event_emit_dict("cat.freq", NULL, cat_msg);
         dict_free(cat_msg);
      } else if (strcasecmp(cmd, "mode") == 0) {
         const char *mode = dict_get(d, "cat.state.mode", NULL);
         if (!mode) mode = dict_get(d, "cat.mode", NULL);

         if (!has_priv(cptr->user->uid, "admin|owner|tx|noob") || cptr->user->is_muted) {
            return true;
         }

         if (!vfo || !mode) {
            Log(LOG_DEBUG, "ws.rigctl", "MODE set without vfo:<%p> or mode:<%p>", vfo, mode);
            return true;
         }
         rr_vfo_t c_vfo;
         char msgbuf[HTTP_WS_MAX_MSG + 1];
         c_vfo = vfo_lookup(vfo[0]);
         cptr->last_cat = now;         // last CAT message received from user
         cptr->last_heard = now;

         // tell everyone about it
         dict *cat_msg = dict_new();
         dict_add(cat_msg, "msg.type", "cat");
         dict_add(cat_msg, "cat.cmd", "mode");
         dict_add(cat_msg, "cat.mode", mode);
         dict_add(cat_msg, "cat.user", cptr->chatname);
         dict_add(cat_msg, "cat.vfo", vfo);
         dict_add_ulong(cat_msg, "msg.ts", now);

         ws_broadcast_dict(NULL, cat_msg, WEBSOCKET_OP_TEXT);
         dict_free(cat_msg);

         Log(LOG_AUDIT, "mode", "User %s set VFO %s MODE to %s", cptr->chatname, vfo, mode);
         rr_mode_t new_mode = vfo_parse_mode(mode);

         if (new_mode != MODE_NONE) {
            // NB: We can't call the backend directly from the library; send
            // a rigctl event for the server program to apply (same path as
            // the !mode chat command uses).
            dict *cmd_d = dict_new();
            dict_add(cmd_d, "msg.type", "rigctl");
            dict_add(cmd_d, "rigctl.cmd", "mode");
            dict_add(cmd_d, "rigctl.mode", mode);
            dict_add(cmd_d, "rigctl.from", cptr->chatname);
            dict_add(cmd_d, "rigctl.vfo", vfo);
            event_emit_dict("rigctl", NULL, cmd_d);
            dict_free(cmd_d);
         } else {
            Log(LOG_WARN, "ws.rigctl", "Couldn't parse mode %s", mode);
         }
      } else {
         const char *jp = dict2json(d);
         Log(LOG_DEBUG, "ws.rigctl", "Got unknown rig msg: |%s|", d);
         ws_send_error(cptr, "Unknown message: |%s|", jp);
         free( (void *)jp );
      }
   }
   return true;
}
