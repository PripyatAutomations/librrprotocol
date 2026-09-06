//
// irc.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Socket backend for io subsys
//
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

bool irc_init(void) {
   // XXX: These need to go into the irc_init() or
   // irc_client_init/irc_server_init functions as appropriate!
   irc_register_default_callbacks();
   irc_register_default_numeric_callbacks();

   return false;
}

//
// This will create a dict containing a restricted set of state things which
// we'll allow
// substituting in log files, messages, etc.
dict *irc_generate_vars(rrconn_t *cptr, const char *chan) {
   dict *d = dict_new();

   if (!d) {
      Log(LOG_CRIT, "irc", "OOM in irc_generate_vars");

      return NULL;
   }

   if (cptr) {
      dict_add(d, "nick", cptr->nick);
   }

   if (chan) {
      dict_add(d, "chan", (char *)chan);
   }

   return d;
}

// Send as much of this user's sendq as we can
static void irc_try_send(rrconn_t *cptr) {
   if (!cptr || cptr->fd <= 0) {
      return;
   }
   size_t len = 0;
   char *p = cptr->sendq;

   // find how much of sendq is complete messages ending with \r\n
   while ( (p = strstr(p, "\r\n") ) ) {
      len = (p - cptr->sendq) + 2;
      p += 2;
   }

   if (len == 0) {
      return;
   }
   ssize_t n = send(cptr->fd, cptr->sendq, len, 0);
   Log(LOG_CRIT, "irc", "send(%d) to cptr:<%p>: %d bytes: %.*s", cptr->fd, cptr, (int)n, (int)len, cptr->sendq);

   if (n < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
         Log( LOG_CRIT, "irc", "send failed: %s", strerror(errno) );
         close(cptr->fd);
         cptr->connected = false;
      }

      return;
   }

   if ( (size_t)n < len) {
      // partial send, move remaining to front
      memmove(cptr->sendq, cptr->sendq + n, len - n);
      cptr->sendq[len - n] = '\0';
   } else {
      // full send, move any leftover queued messages
      size_t remaining = strlen(cptr->sendq + len);

      if (remaining > 0) {
         memmove(cptr->sendq, cptr->sendq + len, remaining + 1);
      } else {
         cptr->sendq[0] = '\0';
      }
   }
}

bool irc_send(rrconn_t *cptr, const char *fmt, ...) {
   if (!cptr || !fmt || cptr->fd <= 0) {
      return false;
   }
   char msg[IRC_MSGLEN];
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(msg, sizeof(msg), fmt, ap);
   va_end(ap);

   size_t msglen = strlen(msg);

   if (msglen + 2 + strlen(cptr->sendq) >= SENDQLEN) {
      Log(LOG_WARN, "irc", "sendq full, dropping message");

      return false;
   }
   // append message + CRLF
   size_t cur_len = strlen(cptr->sendq);
   memcpy(cptr->sendq + cur_len, msg, msglen);
   cur_len += msglen;
   cptr->sendq[cur_len++] = '\r';
   cptr->sendq[cur_len++] = '\n';
   cptr->sendq[cur_len] = '\0';

       // attempt to send immediately
       irc_try_send(cptr);

       // NB: Any leftover data in the sendq will be flushed by the periodic
       // timer / poll loop calling irc_try_send() again, since we don't have
       // an event loop to watch for writability anymore.

       return true;
   }

/*
 * Process incoming data from an IRC connection: read what's available and
 * feed complete lines to irc_process_message(). Called from the poll loop /
 * periodic timer (formerly a libev ev_io callback).
 */
void irc_io_poll(rrconn_t *cptr) {
   if (!cptr || cptr->fd <= 0 || !cptr->connected) {
      return;
   }

   char buf[IRC_MSGLEN];
   ssize_t n = recv(cptr->fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);

   if (n == 0) {
      close(cptr->fd);
      cptr->fd = -1;
      cptr->connected = false;
      return;
   }

   if (n < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
         Log(LOG_CRIT, "irc", "recv failed: %s", strerror(errno));
         close(cptr->fd);
         cptr->fd = -1;
         cptr->connected = false;
      }
      // fallthrough: flush any pending sendq
      irc_try_send(cptr);
      return;
   }

   buf[n] = '\0';

   // append to recvq safely
   size_t cur_len = strlen(cptr->recvq);

   if (cur_len + n >= RECVQLEN) {
      Log(LOG_WARN, "irc", "recvq overflow, resetting");
      cptr->recvq[0] = '\0';
      cur_len = 0;
   }
   memcpy(cptr->recvq + cur_len, buf, n);
   cur_len += n;
   cptr->recvq[cur_len] = '\0';

   // process complete lines
   char *start = cptr->recvq;
   char *end;
   while ( (end = strstr(start, "\r\n") ) ) {
      *end = '\0';
      Log(LOG_DEBUG, "net", "processing line: [%s]", start);
      irc_process_message(cptr, start);

      // send login on first server message
      if (!cptr->sent_login) {
         if (cptr->server->pass[0]) {
            if (cptr->server->account[0]) {
               irc_send(cptr, "PASS %s:%s", cptr->server->account, cptr->server->pass);
            } else {
               irc_send(cptr, "PASS %s", cptr->server->pass);
            }
         }
         irc_send(cptr, "NICK %s", cptr->nick);
         const char *ident = cptr->server->ident[0] ? cptr->server->ident : cptr->nick;
         irc_send(cptr, "USER %s 0 * :%s", ident, cptr->nick);
         cptr->sent_login = true;
      }
      start = end + 2;
   }
   // move leftover partial line to front
   size_t leftover = strlen(start);
   memmove(cptr->recvq, start, leftover + 1);

   // flush any pending sendq data
   irc_try_send(cptr);
}
