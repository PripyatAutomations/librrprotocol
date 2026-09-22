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
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <rrserver/backend.h>

// minimum reason length for kick/ban/etc
#define	CHAT_MIN_REASON_LEN 10
#define CALLSIGN_LOOKUP_START_TIMEOUT_MS 60000

extern time_t now;
extern rrconn_t *http_client_list;
extern bool dying, restarting;
extern const char *config_file;
extern bool ws_chat_err_noprivs(rrconn_t *cptr, const char *action);
extern bool ws_chat_error_need_reason(rrconn_t *cptr, const char *command);

static rr_subproc_t callsign_lookup_process = { .pid = -1, .error_fd = -1 };
static bool callsign_lookup_atexit_registered = false;
static bool callsign_lookup_ready = false;
static bool callsign_lookup_pending = false;
static rrconn_t *callsign_lookup_client = NULL;
static time_t callsign_lookup_deadline = 0;
static char callsign_lookup_request_text[256];
static char callsign_lookup_reply[HTTP_WS_MAX_MSG];
static size_t callsign_lookup_reply_len = 0;

static void callsign_lookup_stop(void) {
   rr_subproc_stop(&callsign_lookup_process, SIGTERM);
   callsign_lookup_ready = false;
   callsign_lookup_pending = false;
   callsign_lookup_client = NULL;
   callsign_lookup_deadline = 0;
   callsign_lookup_request_text[0] = '\0';
   callsign_lookup_reply_len = 0;
}

static bool callsign_lookup_readline(char *line, size_t len, int timeout_ms) {
   return rr_subproc_readline(&callsign_lookup_process, line, len, timeout_ms);
}

static bool callsign_lookup_wait_ready(int timeout_ms) {
   if (callsign_lookup_ready) return true;
   if (callsign_lookup_process.pid <= 0 || !callsign_lookup_process.input ||
       !callsign_lookup_process.output) return false;

   char line[1024];
   while (callsign_lookup_readline(line, sizeof(line), timeout_ms)) {
      if (strncmp(line, "+OK ", 4) == 0) {
         callsign_lookup_ready = true;
         Log(LOG_INFO, "callsign", "lookup helper ready (pid %ld)", (long)callsign_lookup_process.pid);
         return true;
      }
      line[strcspn(line, "\r\n")] = '\0';
      if (*line) Log(LOG_WARN, "callsign", "lookup startup: %s", line);
   }

   /* A zero-time probe is used from the WebSocket event loop.  No data yet
    * means only that startup is still in progress; preserve the helper and
    * let a later request probe it again. */
   if (timeout_ms == 0) {
      int status = 0;
      pid_t ended = waitpid(callsign_lookup_process.pid, &status, WNOHANG);
      if (ended == callsign_lookup_process.pid) {
         if (WIFEXITED(status)) {
            Log(LOG_WARN, "callsign", "lookup exited before ready (status %d)", WEXITSTATUS(status));
         } else if (WIFSIGNALED(status)) {
            Log(LOG_WARN, "callsign", "lookup terminated before ready by signal %d", WTERMSIG(status));
         }
         callsign_lookup_stop();
      }
      return false;
   }

   int status = 0;
   pid_t ended = waitpid(callsign_lookup_process.pid, &status, WNOHANG);
   if (ended == callsign_lookup_process.pid) {
      if (WIFEXITED(status)) {
         Log(LOG_WARN, "callsign", "lookup exited before ready (status %d)", WEXITSTATUS(status));
      } else if (WIFSIGNALED(status)) {
         Log(LOG_WARN, "callsign", "lookup terminated before ready by signal %d", WTERMSIG(status));
      }
   } else {
      char *program = cfg_get_path("callsign-lookup:path");
      Log(LOG_WARN, "callsign", "lookup did not report ready within %d seconds (program=%s, config=%s)",
         (timeout_ms + 999) / 1000, (program && *program) ? program : "(unset)",
         (config_file && *config_file) ? config_file : "(unset)");
      free(program);
   }
   callsign_lookup_stop();
   return false;
}

static bool callsign_lookup_send_reply(rrconn_t *cptr, const char *text) {
   if (!cptr || !text) return false;
   dict *message = dict_new();
   if (!message) return false;
   dict_add(message, "msg.type", "callsign");
   dict_add_ulong(message, "msg.ts", now);
   /* Keep the wire response machine-readable.  The dictionary serializer
    * turns dotted keys into nested JSON objects. */
   const char *line = text;
   bool first = true;
   while (*line) {
      const char *end = strchr(line, '\n');
      size_t line_len = end ? (size_t)(end - line) : strlen(line);
      if (line_len > 0) {
         const char *colon = memchr(line, ':', line_len);
         if (first) {
            char status[256];
            size_t n = line_len < sizeof(status) - 1 ? line_len : sizeof(status) - 1;
            memcpy(status, line, n);
            status[n] = '\0';
            dict_add(message, "callsign.status", status);
            first = false;
         } else if (colon && colon > line) {
            char key[128];
            size_t key_len = (size_t)(colon - line);
            if (key_len >= sizeof(key)) key_len = sizeof(key) - 1;
            size_t out = 0;
            for (size_t i = 0; i < key_len && out + 1 < sizeof(key); i++) {
               unsigned char ch = (unsigned char)line[i];
               if (isalnum(ch)) key[out++] = (char)tolower(ch);
               else if (out > 0 && key[out - 1] != '_') key[out++] = '_';
            }
            while (out > 0 && key[out - 1] == '_') out--;
            key[out] = '\0';
            if (out > 0) {
               char field_key[160];
               snprintf(field_key, sizeof(field_key), "callsign.fields.%s", key);
               char value[1024];
               size_t value_len = line_len - (size_t)(colon - line) - 1;
               if (value_len >= sizeof(value)) value_len = sizeof(value) - 1;
               memcpy(value, colon + 1, value_len);
               value[value_len] = '\0';
               while (*value == ' ') memmove(value, value + 1, strlen(value));
               dict_add(message, field_key, value);
            }
         }
      }
      if (!end) break;
      line = end + 1;
   }
   dict_add_bool(message, "callsign.done", true);
   bool sent = ws_send_dict(NULL, cptr, message, WEBSOCKET_OP_TEXT);
   dict_free(message);
   return sent;
}

static bool callsign_lookup_start(void) {
   if (callsign_lookup_process.pid > 0 && callsign_lookup_process.input &&
       callsign_lookup_process.output) return true;

   char *program = cfg_get_path("callsign-lookup:path");
   if (!program || !*program || !config_file || !*config_file) {
      Log(LOG_WARN, "callsign", "Cannot start lookup: callsign-lookup:path or server config is missing (program=%s, config=%s)",
         (program && *program) ? program : "(unset)",
         (config_file && *config_file) ? config_file : "(unset)");
      free(program);
      return false;
   }
   if (access(program, X_OK) != 0) {
      Log(LOG_WARN, "callsign", "Cannot start lookup: %s is not executable: %s", program, strerror(errno));
      free(program);
      return false;
   }
   if (access(config_file, R_OK) != 0) {
      Log(LOG_WARN, "callsign", "Cannot start lookup: config %s is not readable: %s", config_file, strerror(errno));
      free(program);
      return false;
   }
   if (!callsign_lookup_atexit_registered) {
      atexit(callsign_lookup_stop);
      callsign_lookup_atexit_registered = true;
   }

   const char *argv[] = { program, "-q", "-f", config_file, NULL };
   if (!rr_subproc_spawn(&callsign_lookup_process, program, argv, true)) {
      Log(LOG_WARN, "callsign", "Cannot start lookup: subprocess setup failed for %s: %s", program, strerror(errno));
      callsign_lookup_stop();
      free(program);
      return false;
   }
   free(program);
   return true;
}

// Start the persistent lookup helper during server initialization rather
// than making the first /qrz or /grid request pay the startup cost.
bool ws_callsign_lookup_init(void) {
   if (!callsign_lookup_start()) return false;
   /* The helper is launched before the network listener.  Give its banner a
    * bounded startup window here so the first request cannot race readiness,
    * while keeping the old unbounded 60-second startup hang impossible. */
   if (!callsign_lookup_wait_ready(5000)) {
      Log(LOG_WARN, "callsign", "lookup helper did not become ready during initialization");
   }
   return callsign_lookup_ready;
}

/* Drain helper startup output from the server's periodic tick without ever
 * blocking the WebSocket/media event loop. */
void ws_callsign_lookup_poll(void) {
   if (callsign_lookup_process.pid > 0 && !callsign_lookup_ready) {
      (void)callsign_lookup_wait_ready(0);
   }
   if (!callsign_lookup_pending || !callsign_lookup_ready) return;

   char line[1024];
   while (callsign_lookup_readline(line, sizeof(line), 0)) {
      line[strcspn(line, "\r\n")] = '\0';
      if (!*line || strncmp(line, "+NOTICE ", 8) == 0 ||
          strncmp(line, "+OK ", 4) == 0 || strncmp(line, "+PROTO ", 7) == 0 ||
          strncmp(line, "+GOODBYE", 8) == 0 || line[0] == '[' || line[0] == '<' ||
          strncmp(line, "==", 2) == 0) continue;
      if (strcmp(line, "+EOR") == 0) {
         callsign_lookup_reply[callsign_lookup_reply_len] = '\0';
         if (callsign_lookup_client) {
            callsign_lookup_send_reply(callsign_lookup_client, callsign_lookup_reply);
         }
         callsign_lookup_pending = false;
         callsign_lookup_client = NULL;
         callsign_lookup_deadline = 0;
         callsign_lookup_request_text[0] = '\0';
         return;
      }
      size_t line_len = strlen(line);
      if (callsign_lookup_reply_len + line_len + 2 < sizeof(callsign_lookup_reply)) {
         memcpy(callsign_lookup_reply + callsign_lookup_reply_len, line, line_len);
         callsign_lookup_reply_len += line_len;
         callsign_lookup_reply[callsign_lookup_reply_len++] = '\n';
      }
   }

   if (callsign_lookup_deadline > 0 && now >= callsign_lookup_deadline) {
      if (callsign_lookup_client) {
         ws_send_error(callsign_lookup_client, "Callsign lookup timed out");
      }
      Log(LOG_WARN, "callsign", "lookup helper did not finish request: %s", callsign_lookup_request_text);
      callsign_lookup_stop();
   }
}

static bool callsign_lookup_request(rrconn_t *cptr, const char *request) {
   if (callsign_lookup_pending) return false;
   if (!callsign_lookup_start()) return false;
   /* This runs on the WebSocket/event-loop thread.  Allow only a short grace
    * period for the startup banner to cross the pipe; the long startup wait
    * must remain outside request handling so media cannot be stalled. */
   if (!callsign_lookup_wait_ready(250)) {
      Log(LOG_INFO, "callsign", "lookup helper is still starting; request will need to be retried");
      return false;
   }
   if (!rr_subproc_write_line(&callsign_lookup_process, request)) {
      Log(LOG_WARN, "callsign", "lookup request failed while writing to helper (pid %ld): %s",
         (long)callsign_lookup_process.pid, strerror(errno));
      callsign_lookup_stop();
      return false;
   }
   snprintf(callsign_lookup_request_text, sizeof(callsign_lookup_request_text), "%s", request);
   callsign_lookup_client = cptr;
   callsign_lookup_pending = true;
   callsign_lookup_deadline = now + 30;
   callsign_lookup_reply_len = 0;
   return true;
}

typedef struct {
   char name[128];
   bool has_vfos;
   uint32_t vfo_mask;
} ws_room_meta_t;

static ws_room_meta_t room_meta[32];
static const char *room_canonical(const char *room);

static ws_room_meta_t *room_meta_find(const char *room, bool create) {
   const char *canonical = room_canonical(room);
   for (size_t i = 0; i < sizeof(room_meta) / sizeof(room_meta[0]); i++) {
      if (room_meta[i].name[0] && strcasecmp(room_meta[i].name, canonical) == 0) {
         return &room_meta[i];
      }
   }
   if (!create) return NULL;
   for (size_t i = 0; i < sizeof(room_meta) / sizeof(room_meta[0]); i++) {
      if (!room_meta[i].name[0]) {
         snprintf(room_meta[i].name, sizeof(room_meta[i].name), "%s", canonical);
         return &room_meta[i];
      }
   }
   return NULL;
}

const char *ws_authoritative_room(void) {
   static char room[128];
   char *configured = (char *)cfg_get_exp("station.name");
   const char *name = (configured && *configured) ? configured : "rustyrig";
   snprintf(room, sizeof(room), "#%s-rig0", name);
   free(configured);
   ws_room_meta_t *meta = NULL;
   for (size_t i = 0; i < sizeof(room_meta) / sizeof(room_meta[0]); i++) {
      if (strcasecmp(room_meta[i].name, room) == 0) {
         meta = &room_meta[i];
         break;
      }
      if (!meta && !room_meta[i].name[0]) {
         snprintf(room_meta[i].name, sizeof(room_meta[i].name), "%s", room);
         meta = &room_meta[i];
         break;
      }
   }
   if (meta) {
      int nvfos = cfg_get_int("rig.vfos", 2);
      if (nvfos < 1) nvfos = 1;
      if (nvfos > 32) nvfos = 32;
      meta->has_vfos = true;
      meta->vfo_mask = nvfos == 32 ? UINT32_MAX : ((UINT32_C(1) << nvfos) - 1);
   }
   return room;
}

bool ws_room_has_vfos(const char *room) {
   ws_room_meta_t *meta = room_meta_find(room, false);
   return meta ? meta->has_vfos : false;
}

uint32_t ws_room_vfo_mask(const char *room) {
   ws_room_meta_t *meta = room_meta_find(room, false);
   return meta ? meta->vfo_mask : 0;
}

static const char *room_canonical(const char *room) {
   if (!room || !*room || strcasecmp(room, "&localrig") == 0) {
      return ws_authoritative_room();
   }
   return room;
}

bool ws_client_in_room(const rrconn_t *cptr, const char *room) {
   if (!cptr) {
      return false;
   }
   const char *want = room_canonical(room);
   char copy[AUTOJOIN_LEN];
   snprintf(copy, sizeof(copy), "%s", cptr->rooms);
   char *save = NULL;
   for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
      if (strcasecmp(tok, want) == 0) {
         return true;
      }
   }
   return false;
}

bool ws_client_join_room(rrconn_t *cptr, const char *room) {
   if (!cptr || !room || !*room) {
      return false;
   }
   const char *canonical = room_canonical(room);
   if (canonical[0] != '#' && canonical[0] != '&') {
      return false;
   }
   if (ws_client_in_room(cptr, canonical)) {
      return true;
   }
   size_t used = strlen(cptr->rooms);
   size_t need = strlen(canonical) + (used ? 1 : 0);
   if (used + need + 1 >= sizeof(cptr->rooms)) {
      return false;
   }
   if (used) strlcat(cptr->rooms, ",", sizeof(cptr->rooms));
   strlcat(cptr->rooms, canonical, sizeof(cptr->rooms));
   return true;
}

bool ws_client_part_room(rrconn_t *cptr, const char *room) {
   if (!cptr || !room || strcasecmp(room, "&localrig") == 0 ||
       strcasecmp(room, ws_authoritative_room()) == 0) {
      return false;
   }
   const char *want = room_canonical(room);
   char old[AUTOJOIN_LEN], out[AUTOJOIN_LEN] = "";
   snprintf(old, sizeof(old), "%s", cptr->rooms);
   char *save = NULL;
   for (char *tok = strtok_r(old, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
      if (strcasecmp(tok, want) == 0) continue;
      if (out[0]) strlcat(out, ",", sizeof(out));
      strlcat(out, tok, sizeof(out));
   }
   snprintf(cptr->rooms, sizeof(cptr->rooms), "%s", out);
   return true;
}

void ws_broadcast_room_dict(rrconn_t *sender, dict *d, const char *room) {
   if (!d) return;
   rrconn_t *cur = http_client_list;
   while (cur) {
      if (cur->is_ws && cur->authenticated && ws_client_in_room(cur, room)) {
         ws_send_dict(sender, cur, d, WEBSOCKET_OP_TEXT);
      }
      cur = cur->next;
   }
}

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
      return false;
   }
   // Video sources (webcam etc announcing hello.role: video-source) are not
   // users; keep them out of the chat/user lists
   if (client_has_flag(cptr, FLAG_VIDEO_SOURCE) ) {
      return false;
   }
   dict *talk_msg = dict_new();
   dict_add(talk_msg, "msg.type", "talk");
   dict_add(talk_msg, "talk.privs", cptr->user->privs);
   dict_add(talk_msg, "talk.user", cptr->chatname);
   dict_add(talk_msg, "talk.cmd", "userinfo");
   dict_add_int(talk_msg, "talk.sessions", cptr->user->sessions);
   dict_add_bool(talk_msg, "talk.muted", cptr->user->is_muted);
   dict_add_bool(talk_msg, "talk.tx", cptr->is_ptt);
   dict_add_long(talk_msg, "msg.ts", now);

   if (acptr) {
      ws_send_dict(NULL, acptr, talk_msg, WEBSOCKET_OP_TEXT);
   } else {
      ws_broadcast_dict(NULL, talk_msg, WEBSOCKET_OP_TEXT);
   }

   dict_free(talk_msg);
   return true;
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
         return true;
      }
      current = current->next;
   }
   return true;
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
      return false;
   }

   if (!cptr->user) {
      Log(LOG_WARN, "chat", "talk parse, cptr:<%p> ->user NULL", cptr);
      return false;
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
      if (strcasecmp(cmd, "qrz") == 0 || strcasecmp(cmd, "grid") == 0) {
         if (!data || !*data) {
            ws_send_error(cptr, "Usage: /%s VALUE", strcasecmp(cmd, "grid") == 0 ? "grid" : "qrz");
            return false;
         }
         char lookup_data[256];
         snprintf(lookup_data, sizeof(lookup_data), "%s", data);
         bool no_cache = false;
         if (strcasecmp(cmd, "qrz") == 0) {
            char *option = strpbrk(lookup_data, " \t");
            if (option) {
               *option++ = '\0';
               while (*option == ' ' || *option == '\t') option++;
               if (strcasecmp(option, "nocache") != 0) {
                  ws_send_error(cptr, "Invalid /qrz option: %s", option);
                  return false;
               }
               no_cache = true;
            }
            for (const unsigned char *p = (const unsigned char *)lookup_data; *p; p++) {
               if (!isalnum(*p) && *p != '-' && *p != '/' && *p != '.') {
                  ws_send_error(cptr, "Invalid callsign: %s", lookup_data);
                  return false;
               }
            }
         } else {
            for (const unsigned char *p = (const unsigned char *)lookup_data; *p; p++) {
               if (!isalnum(*p) && *p != '-' && *p != '.' && *p != ',' &&
                   *p != '+' && *p != ' ') {
                  ws_send_error(cptr, "Invalid grid or coordinates: %s", data);
                  return false;
               }
            }
         }
         char *lookup_program = cfg_get_path("callsign-lookup:path");
         bool lookup_configured = lookup_program && *lookup_program &&
            config_file && *config_file;
         free(lookup_program);
         if (!lookup_configured) {
            ws_send_error(cptr, "Callsign lookup is not configured on the server");
            return false;
         }
         char request[256];
         snprintf(request, sizeof(request), "/%s %s",
            strcasecmp(cmd, "grid") == 0 ? "GRID" : "CALL", lookup_data);
         if (no_cache) strncat(request, " NOCACHE", sizeof(request) - strlen(request) - 1);
         if (!callsign_lookup_request(cptr, request)) {
            ws_send_error(cptr, "Callsign lookup is still starting; please retry shortly");
            return false;
         }
         return true;
      } else if (strcasecmp(cmd, "join") == 0 || strcasecmp(cmd, "part") == 0) {
         const char *requested = target ? target : data;
         bool joining = strcasecmp(cmd, "join") == 0;
         bool room_ok = joining ? ws_client_join_room(cptr, requested) :
            ws_client_part_room(cptr, requested);
         if (!room_ok) {
            ws_send_error(cptr, "%s failed for room %s", joining ? "JOIN" : "PART",
               requested ? requested : "(none)");
            return false;
         }
         dict *room_msg = dict_new();
         dict_add(room_msg, "msg.type", "talk");
         dict_add(room_msg, "talk.cmd", joining ? "join" : "part");
         dict_add(room_msg, "talk.target", room_canonical(requested));
         dict_add(room_msg, "talk.room", room_canonical(requested));
         dict_add_bool(room_msg, "room.has-vfos", ws_room_has_vfos(requested));
         dict_add_ulong(room_msg, "room.vfo-mask", ws_room_vfo_mask(requested));
         dict_add(room_msg, "talk.user", cptr->chatname);
         dict_add_ulong(room_msg, "msg.ts", now);
         ws_broadcast_room_dict(cptr, room_msg, room_canonical(requested));
         dict_free(room_msg);
         return true;
      } else if (strcasecmp(cmd, "msg") == 0) {
         if (!data) {
            Log(LOG_DEBUG, "chat",
               "got msg for cptr <%p> with no data: chatname: %s",
               cptr, user);
            return false;
         }

         // If the message is empty, just return success
         if (strlen(data) == 0) {
            Log(LOG_CRAZY, "chat", "talk msg has no data");
            return true;
         }

         if (!has_priv(cptr->user->uid, "admin|owner|chat")) {
            Log(LOG_CRAZY, "chat",
               "user %s doesn't have chat privileges but tried to send a message",
               user);

            // XXX: Alert the user that their message was NOT delivered
            // because they aren't allowed to send it.
            ws_send_error(cptr, "You do not have CHAT privilege.");
            return false;
         }

         // sanity check
         if (!user) {
            Log(LOG_CRAZY, "chat", "talk parse, msg has no user field");
            return false;
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
                        return false;
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

                        // Stop only when there's no command left to parse.
                        // Commands that take no argument (e.g. !help) must
                        // still reach the dispatch below.
                        if (*cmd == '\0') {
                           break;
                        }

                        if (strcasecmp(cmd, "help") == 0) {
                           // XXX: These should move to help/ and get served
                           // via that mechanism.
                           // Plain text, one notice per line: notices are
                           // sent unescaped so both the C client and the
                           // webui can display them as-is. (PARITY:
                           // rustyrig-www/js/webui notice rendering)
                           ws_send_notice(cptr, "***SERVER***");
                           ws_send_notice(cptr, "*** !help for VFO commands ***");
                           ws_send_notice(cptr, "  !freq <freq> - Set frequency to <freq> - can be 7200 7.2m 7200000 etc form");
                           ws_send_notice(cptr, "  !mode <mode> - Set mode to CW|AM|LSB|USB|FM|DL|DU");
                           ws_send_notice(cptr, "  !power <power> - Set power in watts (e.g. !power 25)");
                           ws_send_notice(cptr, "  !vfo <vfo> - Switch VFOs (A|B|C)");
                           ws_send_notice(cptr, "  !width <width> - Set passband width (narrow|normal|wide)");

                           return true;

                        } else if (strcasecmp(cmd, "freq") == 0) {
                          if (*arg == '\0') {
                             ws_send_error(cptr, "!freq requires a frequency argument");
                             return false;
                          }

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
                          if (*arg == '\0') {
                             ws_send_error(cptr, "!mode requires a mode argument");
                             return false;
                          }

                          Log(LOG_DEBUG, "ws.chat",
                              "Got !mode %s from %s",
                              arg, cptr->chatname);

                           rr_mode_t new_mode =
                              vfo_parse_mode(arg);

                           if (new_mode != MODE_NONE) {
                              rr_set_mode(active_vfo, new_mode);

                              // Audit trail is logged by the rigctl event handler
                           }

                        } else if (strcasecmp(cmd, "power") == 0) {
                          if (*arg == '\0') {
                             ws_send_error(cptr, "!power requires a power argument (in watts)");
                             return false;
                          }

                          float real_power = strtof(arg, NULL);

                          Log(LOG_DEBUG, "ws.chat",
                             "Got !power %f (%s) from %s",
                             real_power, arg, cptr->chatname);

                          dict *cmd_d = dict_new();
                          dict_add(cmd_d, "msg.type", "rigctl");
                          dict_add(cmd_d, "rigctl.cmd", "power");
                          dict_add_float(cmd_d, "rigctl.power", real_power);
                          dict_add(cmd_d, "rigctl.from",
                             cptr->chatname);
                          dict_add(cmd_d, "rigctl.vfo",
                             (char *)vfo_name(active_vfo));

                          event_emit_dict("rigctl", NULL, cmd_d);
                          dict_free(cmd_d);

                        } else if (strcasecmp(cmd, "width") == 0) {
                          if (*arg == '\0') {
                             ws_send_error(cptr, "!width requires an argument");
                             return false;
                          }

                          Log(LOG_DEBUG, "ws.chat",
                              "Got !width %s from %s",
                              arg, cptr->chatname);

                           rr_set_width(active_vfo, arg);

                           // Audit trail is logged by the rigctl event handler

                        } else if (strcasecmp(cmd, "vfo") == 0) {
                          if (*arg == '\0') {
                             ws_send_error(cptr, "!vfo requires a vfo argument (A|B|C...)");
                             return false;
                          }

                          Log(LOG_DEBUG, "ws.chat",
                             "Got !vfo %s from %s",
                             arg, cptr->chatname);

                          rr_vfo_t new_vfo = vfo_lookup(toupper(arg[0]) );

                          if (new_vfo < 0 || new_vfo >= MAX_VFOS) {
                             ws_send_error(cptr, "Unknown VFO: !vfo %s (try A-Z)", arg);
                             return false;
                          }

                          if (cptr->ptt_vfo) {
                             ws_send_error(cptr, "Cannot switch VFO while transmitting on VFO %c",
                                cptr->ptt_vfo);
                             return false;
                          }

                          if (new_vfo == active_vfo) {
                             // no-op, but confirm to the user so it doesn't look hung
                             ws_send_notice(cptr, "VFO %s is already active", vfo_name(new_vfo) );
                          } else {
                             Log(LOG_AUDIT, "ws.chat", "User %s switched active VFO %s -> %s",
                                cptr->chatname, vfo_name(active_vfo), vfo_name(new_vfo) );
                             active_vfo = new_vfo;

                             // Nudge the rig poll so the new active VFO's state
                             // gets broadcast promptly (cat.state.vfo etc)
                             // NB: can't call rr_be_poll from the library; ask
                             // the server to poll via an event
                             event_emit("be.poll", NULL, NULL);
                          }

                        } else {
                           Log(LOG_WARN, "ws.chat",
                              "Unknown command: %s", cmd);
                           ws_send_error(cptr, "Unknown command: !%s - try !help", cmd);
                           return false;
                        }
                     }

                     // These events shouldn't get relayed because the CAT
                     // events generated above will be relayed separately.
                     return true;
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
               return true;

            } else {
               Log(LOG_DEBUG, "ws.chat",
                  "unknown message type: %s", msg_type);
            }
         }
      } else if (strcasecmp(cmd, "whois") == 0) {
         if (!target) {
            Log(LOG_DEBUG, "chat", "whois with no target");
            ws_send_error(cptr, "No target given for WHOIS");
            return false;
         }

         rrconn_t *acptr = http_find_client_by_name(target);

         if (!acptr || !acptr->user) {
            ws_send_error(cptr, "WHOIS: no such user: %s", target);
            return false;
         }

         // Flat whois reply, keyed off talk.<field> - shared by webui and
         // rrclient (chat.whois.c). Sessions is just the count (also in userinfo).
         dict *wi = dict_new();

         dict_add(wi, "msg.type", "talk");
         dict_add(wi, "talk.cmd", "whois");
         dict_add_ulong(wi, "msg.ts", now);

         dict_add(wi, "talk.username", acptr->user->name);
         dict_add(wi, "talk.email", acptr->user->email);
         dict_add(wi, "talk.privs", acptr->user->privs);
         dict_add_bool(wi, "talk.muted", acptr->user->is_muted);
         dict_add_int(wi, "talk.sessions", acptr->user->sessions);

         // Session info of the first matching connection
         dict_add_ulong(wi, "talk.connected", (unsigned long)acptr->session_start);
         dict_add_ulong(wi, "talk.last_heard", (unsigned long)acptr->last_heard);
         dict_add(wi, "talk.ua", acptr->user_agent ? acptr->user_agent : "unknown");

         ws_send_dict(NULL, cptr, wi, WEBSOCKET_OP_TEXT);
         dict_free(wi);
      } else if (strcasecmp(cmd, "die") == 0) {
         ws_chat_cmd_die(cptr, reason);

      } else if (strcasecmp(cmd, "quota") == 0) {
         // PARITY: rustyrig-www/js/webui.chat.js /quota (sends talk.cmd=quota)
         // Admin/owner only; the server program does the quota work via the
         // quota.cmd event (sqlite lives in rrserver, not in the library)
         if (!has_priv(cptr->user->uid, "admin|owner") ) {
            ws_chat_err_noprivs(cptr, "QUOTA");
            return false;
         }

         // data is the command tail: LIST | SHOW <user>... | ADD <mins> <user>...
         // | RESET <user>... | SET <mins> <user>...; target carries a single
         // user name for the SHOW case (webui passes it via talk.target)
         dict *q = dict_new();

         if (!q) {
            Log(LOG_WARN, "chat", "quota cmd: failed to create dict");
            return false;
         }
         dict_add(q, "msg.type", "quota.cmd");
         dict_add(q, "quota.from", cptr->chatname);
         dict_add(q, "quota.data", data ? data : "");

         if (target) {
            dict_add(q, "quota.target", target);
         }
         event_emit_dict("quota.cmd", cptr, q);
         dict_free(q);

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

      } else if (strcasecmp(cmd, "media") == 0) {
         // Media channel admin: /media LIST | SUBSCRIBE <uuid|#> |
         // UNSUBSCRIBE <uuid|#> | REMOVE <uuid|#>. LIST/SUBSCRIBE/UNSUBSCRIBE
         // are available to any authenticated user; REMOVE needs admin|owner.
         // PARITY: rustyrig-www/js/webui.media.js (channel management)
         char sub[32] = "LIST";
         char marg[64] = "";

         if (data && data[0] != '\0') {
            size_t i = 0;

            while (*data && !isspace( (unsigned char)*data) && i < sizeof(sub) - 1) {
               sub[i++] = *data++;
            }
            sub[i] = '\0';
            while (isspace( (unsigned char)*data) ) {
               data++;
            }
            strlcpy(marg, data, sizeof(marg) );
         }
         if (strcasecmp(sub, "REMOVE") == 0 && !has_priv(cptr->user->uid, "admin|owner") ) {
            ws_chat_err_noprivs(cptr, "MEDIA REMOVE");
            return false;
         }
         dict *m = dict_new();

         if (!m) {
            return false;
         }
         if (strcasecmp(sub, "REMOVE") == 0) {
            // Server program (rrserver) owns removal: it notifies all clients
            // via media.chan-remove and clears the registry entry.
            dict_add(m, "media.chan-uuid", marg);
            event_emit_dict("remove-media-channel", cptr, m);
            dict_free(m);

            return true;
         }
         dict_add(m, "msg.type", "media");
         dict_add(m, "media.cmd", strcasecmp(sub, "UNSUBSCRIBE") == 0 ? "unsubscribe" :
                                 (strcasecmp(sub, "SUBSCRIBE") == 0 ? "subscribe" : "list"));
         dict_add_ulong(m, "media.ts", now);

         if (marg[0] != '\0') {
            dict_add(m, "media.chan-uuid", marg);
         }
         ws_handle_mediachan_msg(cptr, m);
         dict_free(m);

      } else if (strcasecmp(cmd, "unmute") == 0) {
         ws_chat_cmd_unmute(cptr, target);
      } else {
         Log(LOG_WARN, "chat", "Unknown command: %s", cmd);
         ws_send_error(cptr, "Invalid command: %s", cmd);
         return false;
      }
   }

   return true;
}
