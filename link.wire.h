//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Dedicated server-link wire format; distinct from client rustyrig.v2.
#ifndef RR_LINK_WIRE_H
#define RR_LINK_WIRE_H
#include <stddef.h>
#include <librustyaxe/core.h>
#define RR_LINK_SUBPROTOCOL "rustyrig-link.v2"
#define RR_LINK_MAX_FRAME 32768
/* Flat JSON string fields, operation-specific whitelist, bounded messages. */
dict *rr_link_wire_decode(const char *data, size_t length);
/* Optional safe diagnostic: no wire field contents are included in reason. */
dict *rr_link_wire_decode_ex(const char *data, size_t length, char *reason, size_t capacity);
char *rr_link_wire_encode(dict *message);
#endif
