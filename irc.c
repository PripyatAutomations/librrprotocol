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
#include <librrprotocol/irc.h>
#include <poll.h>

static void irc_close_connection(rrconn_t *cptr) {
   if (cptr->fd < 0) {
      return;
   }
   close(cptr->fd);
   cptr->fd = -1;
   cptr->connected = 0;
   cptr->authenticated = false;
   cptr->sent_login = false;
   cptr->sendq[0] = cptr->recvq[0] = '\0';
   event_emit("irc.disconnected", cptr, "");
}

bool irc_init(void) {
   if (!irc_register_default_callbacks() || !irc_register_default_numeric_callbacks()) {
      irc_shutdown();

      return true;
   }

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
   if (!cptr || cptr->fd < 0) {
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
   ssize_t n = send(cptr->fd, cptr->sendq, len, MSG_DONTWAIT | MSG_NOSIGNAL);

   if (n < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
         Log(LOG_CRIT, "irc", "send failed: %s", strerror(errno) );
         irc_close_connection(cptr);
      }

      return;
   }

   size_t queued = strlen(cptr->sendq);
   memmove(cptr->sendq, cptr->sendq + n, queued - (size_t)n + 1);

}

bool irc_send(rrconn_t *cptr, const char *fmt, ...) {
   if (!cptr || !fmt || cptr->fd < 0) {
      return false;
   }
   char msg[IRC_MSGLEN];
   va_list ap;
   va_start(ap, fmt);
   int written = vsnprintf(msg, sizeof(msg), fmt, ap);
   va_end(ap);

   if (written < 0 || written > IRC_MSGLEN - 2 || strchr(msg, '\r') || strchr(msg, '\n')) {
      return false;
   }
   size_t msglen = (size_t)written;

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
   if (cptr->connected) {
      irc_try_send(cptr);
   }

   // NB: Any leftover data in the sendq will be flushed by the periodic
   // timer / poll loop calling irc_try_send() again, since we don't have
   // an event loop to watch for writability anymore.

   return cptr->fd >= 0;
}

/*
 * Process incoming data from an IRC connection: read what's available and feed complete lines to irc_process_message(). Called from the poll loop /
 * periodic timer (formerly a libev ev_io callback).
 */
void irc_io_poll(rrconn_t *cptr) {
   if (!cptr || cptr->fd < 0 || !cptr->server) {
      return;
   }

   if (!cptr->connected) {
      struct pollfd socket_poll = {
         .fd = cptr->fd, .events = POLLOUT
      };
      int ready = poll(&socket_poll, 1, 0);

      if (ready <= 0) {
         return;
      }
      int error = 0;
      socklen_t length = sizeof(error);

      if (getsockopt(cptr->fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error) {
         irc_close_connection(cptr);

         return;
      }
      cptr->connected = now;
   }

   if (!cptr->sent_login) {
      bool sent = true;

      if (cptr->server->pass[0]) {
         if (cptr->server->account[0]) {
            sent = irc_send(cptr, "PASS %s:%s", cptr->server->account, cptr->server->pass);
         } else {
            sent = irc_send(cptr, "PASS %s", cptr->server->pass);
         }
      }
      const char *ident = cptr->server->ident[0] ? cptr->server->ident : cptr->nick;
      sent = sent && irc_send(cptr, "NICK %s", cptr->nick) &&
         irc_send(cptr, "USER %s 0 * :%s", ident, cptr->nick);

      if (!sent || cptr->fd < 0) {
         irc_close_connection(cptr);

         return;
      }
      cptr->sent_login = true;
   }

   char buf[IRC_MSGLEN];
   ssize_t n = recv(cptr->fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);

   if (n == 0) {
      irc_close_connection(cptr);

      return;
   }

   if (n < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
         Log(LOG_CRIT, "irc", "recv failed: %s", strerror(errno) );
         irc_close_connection(cptr);
      }
      // fallthrough: flush any pending sendq
      irc_try_send(cptr);

      return;
   }

   buf[n] = '\0';

   // append to recvq safely
   size_t cur_len = strlen(cptr->recvq);

   if (cur_len + n >= RECVQLEN) {
      Log(LOG_WARN, "irc", "recvq overflow, closing connection");
      irc_close_connection(cptr);

      return;
   }

   if (memchr(buf, '\0', (size_t)n)) {
      irc_close_connection(cptr);

      return;
   }
   memcpy(cptr->recvq + cur_len, buf, n);
   cur_len += n;
   cptr->recvq[cur_len] = '\0';

   // process complete lines
   char *start = cptr->recvq;
   char *end;
   while ( (end = strstr(start, "\r\n") ) ) {
      if ((size_t)(end - start) > IRC_MSGLEN - 2) {
         irc_close_connection(cptr);

         return;
      }
      *end = '\0';
      Log(LOG_DEBUG, "net", "processing line: [%s]", start);
      cptr->last_heard = now;
      irc_process_message(cptr, start);

      if (cptr->fd < 0) {
         return;
      }

      start = end + 2;
   }
   // move leftover partial line to front
   size_t leftover = strlen(start);

   if (leftover > IRC_MSGLEN - 1) {
      irc_close_connection(cptr);

      return;
   }
   memmove(cptr->recvq, start, leftover + 1);

   // flush any pending sendq data
   irc_try_send(cptr);
}
