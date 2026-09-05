//
// ws.chat.c
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

// minimum reason length for kick/ban/etc
#define	CHAT_MIN_REASON_LEN 10

extern time_t now;
extern bool dying, restarting;
extern bool ws_chat_err_noprivs(rrconn_t *cptr, const char *action);
extern bool ws_chat_error_need_reason(rrconn_t *cptr, const char *command);

///////////////////////////////
// DIE: Makes the server die //
///////////////////////////////
static bool ws_chat_cmd_die(rrconn_t *cptr, const char *reason) {
   if (!cptr) {
      return true;
   }

   if (!reason || strlen(reason) < CHAT_MIN_REASON_LEN) {
      ws_chat_error_need_reason(cptr, "die");
      return true;
   }

   if (!cptr->user) {
      return true;
   }

   if (client_has_flag(cptr, FLAG_STAFF) ) {
      // Send an ALERT to all connected users
      char msgbuf[HTTP_WS_MAX_MSG + 1];
      prepare_msg(msgbuf, sizeof(msgbuf), "Shutting down due to /die \"%s\" from %s (uid: %d with privs %s)",
         (reason ? reason : "No reason given"), cptr->chatname, cptr->user->uid, cptr->user->privs);
      send_global_alert("***SERVER***", msgbuf);
      // Throw a shutdown event
      event_emit("shutdown", NULL, NULL);

      // XXX: This should move to the shutdown event handler?
      dying = 1;
   } else {
      ws_chat_err_noprivs(cptr, "DIE");
      return true;
   }

   return false;
}

//////////////////////////////////////
// RESTART: Make the server restart //
//////////////////////////////////////
static bool ws_chat_cmd_restart(rrconn_t *cptr, const char *reason) {
   if (!cptr) {
      return true;
   }

   if (!reason || strlen(reason) < CHAT_MIN_REASON_LEN) {
      ws_chat_error_need_reason(cptr, "RESTART");
      return true;
   }

   if (!cptr->user) {
      return true;
   }

   if (client_has_flag(cptr, FLAG_STAFF) ) {
      // Send an ALERT to all connected users
      char msgbuf[HTTP_WS_MAX_MSG + 1];
      prepare_msg(msgbuf, sizeof(msgbuf), "Shutting down due to /restart from %s (uid: %d with privs %s): %s",
         cptr->chatname, cptr->user->uid, cptr->user->privs, reason);
      send_global_alert("***SERVER***", msgbuf);
      dying = 1;                 // flag that this should be the last iteration
      restarting = 1;            // flag that we should restart after processing
                                 // the alert
   } else {
      ws_chat_err_noprivs(cptr, "RESTART");

      return true;
   }

   return false;
}

///////////////////////
// KICK: Kick a user //
///////////////////////
static bool ws_chat_cmd_kick(rrconn_t *cptr, const char *target, const char *reason) {
   if (!cptr) {
      return true;
   }

   if (!target) {
      // XXX: send an error response 'No target given'
      ws_send_error(cptr, "No target given for KICK");
      return true;
   }

   if (!reason || strlen(reason) < CHAT_MIN_REASON_LEN) {
      ws_chat_error_need_reason(cptr, "kick");
      return true;
   }

   if (client_has_flag(cptr, FLAG_STAFF) ) {
      rrconn_t *acptr;
      int kicked = 0;

      for (acptr = http_client_list ; acptr ; acptr = acptr->next) {
         // skip this one as it's not a valid chat client
         if (!acptr->active || !acptr->is_ws || acptr->chatname[0] == '\0') {
            continue;
         }

         if (strcmp(acptr->chatname, target) == 0) {
            // Build and send message
            char msgbuf[HTTP_WS_MAX_MSG + 1];
            prepare_msg( msgbuf, sizeof(msgbuf), "kicked by %s (Reason: %s)", cptr->chatname,
               (reason ? reason : "No reason given") );
            Log(LOG_AUDIT, "admin.kick", "%s %s", acptr->chatname, msgbuf);
#ifdef	USE_MONGOOSE
            struct mg_str ms = mg_str(msgbuf);
            ws_broadcast_with_flags(FLAG_STAFF, NULL, &ms, WEBSOCKET_OP_TEXT);
            ws_kick_client(acptr, msgbuf);
#endif	// USE_MONGOOSE
            kicked++;
         }
      }

      if (!kicked) {
         char msgbuf[HTTP_WS_MAX_MSG + 1];
         prepare_msg(msgbuf, sizeof(msgbuf), "KICK '%s' command matched no connected users", now, target);
         dict *err_msg = dict_new();
         dict_add(err_msg, "error.msg", msgbuf);
         dict_add_ulong(err_msg, "error.ts", now);

         ws_send_dict(NULL, cptr, err_msg, WEBSOCKET_OP_TEXT);
         dict_free(err_msg);
      }
   } else {
      ws_chat_err_noprivs(cptr, "KICK");
      return true;
   }

   return false;
}

// Send the updated userinfo for a single user; see ws_send_users below for
// everyone
bool ws_send_userinfo(rrconn_t *cptr, rrconn_t *acptr) {
   if (!cptr || !cptr->authenticated || !cptr->user) {
      return true;
   }
   dict *talk_msg = dict_new();
   dict_add(talk_msg, "msg.type", "talk");
   dict_add(talk_msg, "talk.privs", cptr->user->privs);
   dict_add(talk_msg, "talk.user", cptr->chatname);
   dict_add(talk_msg, "talk.cmd", "userinfo");
   dict_add_int(talk_msg, "talk.clones", cptr->user->clones);
   dict_add_bool(talk_msg, "talk.muted", cptr->user->is_muted);
   dict_add_bool(talk_msg, "talk.tx", cptr->is_ptt);
   dict_add_long(talk_msg, "msg.ts", now);

   if (acptr) {
      ws_send_dict(NULL, acptr, talk_msg, WEBSOCKET_OP_TEXT);
   } else {
      ws_broadcast_dict(NULL, talk_msg, WEBSOCKET_OP_TEXT);
   }

   dict_free(talk_msg);
   return false;
}

// Send info on all online users to the user
bool ws_send_users(rrconn_t *cptr) {
   rrconn_t *current = http_client_list;

   // iterate over all the users
   while (current) {
      // should this be sent to a single user?
      if (cptr) {
         ws_send_userinfo(current, cptr);
      } else {
         // nope, broadcast it
         ws_send_userinfo(current, NULL);
      }

      if (!current->next) {
         return false;
      }
      current = current->next;
   }
   return false;
}

///////////////////////
// MUTE: Mute a user //
///////////////////////
static bool ws_chat_cmd_mute(rrconn_t *cptr, const char *target, const char *reason) {
   if (!cptr || !cptr->user) {
      return true;
   }

   if (!target) {
      ws_send_error(cptr, "No target given for MUTE");

      return true;
   }

   if (client_has_flag(cptr, FLAG_STAFF) ) {
      rrconn_t *acptr = http_find_client_by_name(target);

      if (!acptr) {
         return true;
      }
      acptr->user->is_muted = true;

      // Send an ALERT to all connected users
      char msgbuf[HTTP_WS_MAX_MSG + 1];
      prepare_msg( msgbuf, sizeof(msgbuf), "%s MUTEd by %s: Reason: %s", target, cptr->chatname,
         (reason ? reason : "No reason given") );
      send_global_alert("***SERVER***", msgbuf);

      // broadcast the userinfo so cul updates
      ws_send_userinfo(acptr, NULL);

      // turn off PTT if this user holds it
      if (acptr->is_ptt) {
         // XXX: This needs to include which rig/ptt, user, etc
         event_emit("ptt.off", NULL, NULL);
         acptr->is_ptt = false;
      }
   } else {
      ws_chat_err_noprivs(cptr, "MUTE");
      return true;
   }
   return false;
}

///////////////////////////
// UNMUTE: Unmute a user //
///////////////////////////
static bool ws_chat_cmd_unmute(rrconn_t *cptr, const char *target) {
   if (!cptr) {
      return true;
   }

   if (!target) {
      ws_send_error(cptr, "No target given for UNMUTE");
      return true;
   }

   if (!cptr->user) {
      return true;
   }

   if (client_has_flag(cptr, FLAG_STAFF) ) {
      rrconn_t *acptr = http_find_client_by_name(target);

      if (!acptr) {
         return true;
      }
      acptr->user->is_muted = false;

      // Send an ALERT to all connected users
      char msgbuf[HTTP_WS_MAX_MSG + 1];
      prepare_msg(msgbuf, sizeof(msgbuf), "%s UNMUTEd by %s", target, cptr->chatname);
      send_global_alert("***SERVER***", msgbuf);
      // broadcast the userinfo so cul updates
      ws_send_userinfo(acptr, NULL);
   } else {
      ws_chat_err_noprivs(cptr, "UNMUTE");

      return true;
   }

   return false;
}

// Toggle syslog
static bool ws_chat_cmd_syslog(rrconn_t *cptr, const char *state) {
   if (!cptr || !state) {
      return true;
   }

   if (client_has_flag(cptr, FLAG_STAFF) || client_has_flag(cptr, FLAG_SYSLOG) ) {
      bool new_state = false;

      new_state = parse_bool(state);

      if (new_state) {
         client_set_flag(cptr, FLAG_SYSLOG);
      } else {
         client_clear_flag(cptr, FLAG_SYSLOG);
      }
   } else {
      ws_chat_err_noprivs(cptr, "SYSLOG");
      return true;
   }
   return false;
}

bool ws_handle_chat_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      return true;
   }

   if (!cptr->user) {
      Log(LOG_WARN, "chat", "talk parse, cptr:<%p> ->user NULL", cptr);
      return true;
   }

   cptr->last_heard = now;
   cptr->last_chat = now;

   const char *token = dict_get(d, "talk.token", NULL);
   const char *cmd = dict_get(d, "talk.cmd", NULL);
   const char *data = dict_get(d, "talk.data", NULL);
   const char *target = dict_get(d, "talk.target", NULL);
   const char *reason = dict_get(d, "talk.args.reason", NULL);
   const char *msg_type = dict_get(d, "talk.msg_type", NULL);
   const char *user = cptr->chatname;

   // set a default of &localrig, but use target if passed
   const char *channel = "&localrig";

   if (target) {
      channel = target;
   }

   if (cmd) {
      if (strcasecmp(cmd, "msg") == 0) {
         if (!data) {
            Log(LOG_DEBUG, "chat",
               "got msg for cptr <%p> with no data: chatname: %s",
               cptr, user);
            return true;
         }

         // If the message is empty, just return success
         if (strlen(data) == 0) {
            Log(LOG_CRAZY, "chat", "talk msg has no data");
            return false;
         }

         if (!has_priv(cptr->user->uid, "admin|owner|chat")) {
            Log(LOG_CRAZY, "chat",
               "user %s doesn't have chat privileges but tried to send a message",
               user);

            // XXX: Alert the user that their message was NOT delivered
            // because they aren't allowed to send it.
            ws_send_error(cptr, "You do not have CHAT privilege.");
            return true;
         }

         // sanity check
         if (!user) {
            Log(LOG_CRAZY, "chat", "talk parse, msg has no user field");
            return true;
         }

         if (msg_type) {
            if (strcasecmp(msg_type, "file_chunk") == 0 ||
                strcasecmp(msg_type, "pub") == 0 ||
                strcasecmp(msg_type, "action") == 0) {

               /*
                * Commands are handled locally and don't become chat
                * messages. The resulting CAT events are handled/relayed
                * separately.
                */
               if (strcasecmp(msg_type, "pub") == 0 ||
                   strcasecmp(msg_type, "action") == 0) {

                  if (data[0] == '!') {
                     const char *input = data;
                     char cmd[16], arg[32];
                     size_t cmd_len = sizeof(cmd);
                     size_t arg_len = sizeof(arg);

                     if (!has_priv(cptr->user->uid, "admin|owner|tx|noob") ||
                         cptr->user->is_muted) {
                        /// XXX: we should send an error alert
                        return true;
                     }

                     while (*input) {
                        while (isspace(*input) || (*input == '!')) {
                           input++;
                        }

                        // extract command
                        size_t i = 0;

                        while (*input &&
                               !isspace(*input) &&
                               i < cmd_len - 1) {
                           cmd[i++] = *input++;
                        }

                        cmd[i] = '\0';

                        while (isspace(*input)) {
                           input++;
                        }

                        // extract argument
                        i = 0;

                        while (*input &&
                               !isspace(*input) &&
                               i < arg_len - 1) {
                           arg[i++] = *input++;
                        }

                        arg[i] = '\0';

                        if (*cmd == '\0' || *arg == '\0') {
                           break;
                        }

                        if (strcasecmp(cmd, "help") == 0) {
                           // XXX: These should move to help/ and get served
                           // via that mechanism.
                           ws_send_notice(cptr, "<span>***SERVER***"
                              "<br/>*** !help for VFO commands ***<br>"
                              "&nbsp;&nbsp;&nbsp;!freq <freq> - Set frequency to <freq> - can be 7200 7.2m 7200000 etc form<br/>"
                              "&nbsp;&nbsp;&nbsp;!mode <mode> - Set mode to CW|AM|LSB|USB|FM|DL|DU<br/>"
                              "&nbsp;&nbsp;&nbsp;!power <power> - Set power (NYI)<br/>"
                              "&nbsp;&nbsp;&nbsp;!vfo <vfo> - Switch VFOs (A|B|C)<br/>"
                              "&nbsp;&nbsp;&nbsp;!width <width> - Set passband width (narrow|normal|wide)<br/></span>");

                           return false;

                        } else if (strcasecmp(cmd, "freq") == 0) {
                           long real_freq = parse_freq(arg);

                           Log(LOG_DEBUG, "ws.chat",
                              "Got !freq %lu (%s) from %s",
                              real_freq, arg, cptr->chatname);

                           dict *cmd_d = dict_new();
                           dict_add(cmd_d, "msg.type", "rigctl");
                           dict_add(cmd_d, "rigctl.cmd", "freq");
                           dict_add_int(cmd_d, "rigctl.freq", real_freq);
                           dict_add(cmd_d, "rigctl.from",
                              cptr->chatname);
                           dict_add(cmd_d, "rigctl.vfo",
                              (char *)vfo_name(active_vfo));

                           event_emit_dict("rigctl", NULL, cmd_d);
                           dict_free(cmd_d);

                        } else if (strcasecmp(cmd, "mode") == 0) {
                           Log(LOG_DEBUG, "ws.chat",
                              "Got !mode %s from %s",
                              arg, cptr->chatname);

                           rr_mode_t new_mode =
                              vfo_parse_mode(arg);

                           if (new_mode != MODE_NONE) {
                              rr_set_mode(active_vfo, new_mode);

                              // Audit trail: who changed the mode
                              Log(LOG_AUDIT, "ws.chat", "User %s set VFO %s MODE to %s",
                                 cptr->chatname, vfo_name(active_vfo), arg);
                           }

                        } else if (strcasecmp(cmd, "power") == 0) {
                           Log(LOG_DEBUG, "ws.chat",
                              "Got !power %s from %s",
                              arg, cptr->chatname);

                        } else if (strcasecmp(cmd, "width") == 0) {
                           Log(LOG_DEBUG, "ws.chat",
                              "Got !width %s from %s",
                              arg, cptr->chatname);

                           rr_set_width(active_vfo, arg);

                           // Audit trail: who changed the passband width
                           Log(LOG_AUDIT, "ws.chat", "User %s set VFO %s WIDTH to %s",
                              cptr->chatname, vfo_name(active_vfo), arg);

                        } else if (strcasecmp(cmd, "vfo") == 0) {
                           Log(LOG_DEBUG, "ws.chat",
                              "Got !vfo %s from %s",
                              arg, cptr->chatname);

                        } else {
                           Log(LOG_WARN, "ws.chat",
                              "Unknown command: %s", cmd);
                           return false;
                        }
                     }

                     // These events shouldn't get relayed because the CAT
                     // events generated above will be relayed separately.
                     return false;
                  }
               }

               /*
                * Normal chat message, or file chunk.
                *
                * The protocol layer creates the semantic event. The
                * rrserver event handler is responsible for broadcasting,
                * logging, persistence, and other server-side actions.
                */
               bool global_msg = false;

               if (channel[0] != '&') {
                  // Send the message to all connected servers.
                  global_msg = true;
               }

               dict *talk_msg = dict_new();

               dict_add(talk_msg, "msg.type", "talk");
               dict_add(talk_msg, "talk.cmd", "msg");
               dict_add(talk_msg, "talk.data", data);
               dict_add(talk_msg, "talk.from", cptr->chatname);
               dict_add(talk_msg, "talk.target", channel);
               dict_add(talk_msg, "talk.msg_type", msg_type);
               dict_add_bool(talk_msg, "talk.msg.global", global_msg);
               dict_add_ulong(talk_msg, "msg.ts", now);

               /*
                * File chunks need their additional metadata preserved.
                */
               if (strcasecmp(msg_type, "file_chunk") == 0) {
                  const char *filetype =
                     dict_get(d, "talk.filetype", NULL);
                  const char *filename =
                     dict_get(d, "talk.filename", NULL);

                  long chunk_index =
                     dict_get_long(d, "talk.chunk_index", 0);

                  long total_chunks =
                     dict_get_long(d, "talk.total_chunks", 0);

                  dict_add_double(talk_msg,
                     "talk.chunk_index", chunk_index);

                  dict_add_double(talk_msg,
                     "talk.total_chunks", total_chunks);

                  dict_add(talk_msg,
                     "talk.filename", filename);

                  dict_add(talk_msg,
                     "talk.filetype", filetype);
               }

               Log(LOG_CRAZY, "ws.chat",
                  "Emitting talk.msg event: from=<%s> target=<%s> type=<%s> data=<%s>",
                  cptr->chatname,
                  channel,
                  msg_type,
                  data);

               event_emit_dict("talk.msg", cptr, talk_msg);

               Log(LOG_CRAZY, "ws.chat",
                  "Returned from talk.msg event");

               dict_free(talk_msg);
               return false;

            } else {
               Log(LOG_DEBUG, "ws.chat",
                  "unknown message type: %s", msg_type);
            }
         }
      } else if (strcasecmp(cmd, "whois") == 0) {
         if (!target) {
            Log(LOG_DEBUG, "chat", "whois with no target");
            return true;
         }

         rrconn_t *acptr = http_client_list;

         if (!acptr) {
            Log(LOG_DEBUG, "chat", "whois no users online?!?");
            return true;
         }

         /*
          * Existing whois handling continues here.
          */
      } else if (strcasecmp(cmd, "die") == 0) {
         ws_chat_cmd_die(cptr, reason);

      } else if (strcasecmp(cmd, "kick") == 0) {
         ws_chat_cmd_kick(cptr, target, reason);

      } else if (strcasecmp(cmd, "mute") == 0) {
         ws_chat_cmd_mute(cptr, target, reason);

      } else if (strcasecmp(cmd, "names") == 0) {
         ws_send_users(cptr);

      } else if (strcasecmp(cmd, "restart") == 0) {
         ws_chat_cmd_restart(cptr, reason);

      } else if (strcasecmp(cmd, "syslog") == 0) {
         ws_chat_cmd_syslog(cptr, target);

      } else if (strcasecmp(cmd, "unmute") == 0) {
         ws_chat_cmd_unmute(cptr, target);
      }
   }

   return true;
}
