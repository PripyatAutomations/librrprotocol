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

// Send an error message to the user, informing them they lack the appropriate
// privileges in chat
bool ws_chat_err_noprivs(http_client_t *cptr, const char *action) {
   if (!action || !cptr) {
      return true;
   }

   if (!cptr->user) {
      return true;
   }
   Log(LOG_CRAZY, "core", "Unprivileged user %s (uid: %d with privs %s) requested to do %s and was denied",
      cptr->chatname, cptr->user->uid, cptr->user->privs, action);
   char msgbuf[HTTP_WS_MAX_MSG + 1];
   prepare_msg(msgbuf, sizeof(msgbuf), "You do not have enough privileges to use '%s' command", now, action);
   const char *jp = dict2json_mkstr(VAL_STR, "error.msg", msgbuf, VAL_LONG, "error.ts", now);

#if     defined(USE_MONGOOSE)
   mg_ws_send(cptr->conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
#endif
   free( (char *)jp );

   return false;
}

bool ws_chat_error_need_reason(http_client_t *cptr, const char *command) {
   if (!cptr || !command) {
      return true;
   }
   char msgbuf[HTTP_WS_MAX_MSG + 1];
   prepare_msg(msgbuf, sizeof(msgbuf), "You MUST provide a reason for using'%s' command", now, command);

   const char *jp = dict2json_mkstr(VAL_STR, "error.msg", msgbuf, VAL_LONG, "error.ts", now);

#if     defined(USE_MONGOOSE)
   mg_ws_send(cptr->conn, jp, strlen(jp), WEBSOCKET_OP_TEXT);
#endif
   free( (char *)jp );

   return false;
}
