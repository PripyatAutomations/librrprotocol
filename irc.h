//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__librrprotocol_irc_h)
#define __librrprotocol_irc_h

#include <librustyaxe/list.h>
#include <librustyaxe/struct.h>
#include <librrprotocol/irc.types.h>
// CAPABilities crud
#include <librrprotocol/irc.capab.h>

// channel and user modes
#include <librrprotocol/irc.modes.h>

// IRC commands
#include <librrprotocol/irc.commands.h>

// Numeric responses from servers
#include <librrprotocol/irc.numerics.h>

// core protocol parser
#include <librrprotocol/irc.parser.h>
#include <librrprotocol/cli.irc.h>
#include <librrprotocol/srv.irc.core.h>

// Channel stuff
#include <librrprotocol/irc.channel.h>

extern bool irc_init(void);
extern void irc_shutdown(void);
extern void irc_io_poll(rrconn_t *cptr);
extern void irc_message_free(irc_message_t *mp);
/* Events contain JSON: msg.cmd, msg.prefix, msg.argc, msg.arg0 ... . */
extern void irc_emit_message(const char *event, rrconn_t *cptr, const irc_message_t *mp);
extern bool irc_send(rrconn_t *cptr, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static inline char *irc_name(rrconn_t *cptr) {
   if (cptr && cptr->server && cptr->server->network[0]) {
      return cptr->server->network;
   } else if (cptr && cptr->nick[0]) {
      return cptr->nick;
   } else if (cptr && cptr->hostname[0]) {
      return cptr->hostname;
   }

   return "irc";
}

#endif // !defined(__librrprotocol_irc_h)
