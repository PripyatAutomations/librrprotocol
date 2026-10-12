// Dedicated server-link wire format; distinct from client rustyrig.v1.
#ifndef RR_LINK_WIRE_H
#define RR_LINK_WIRE_H
#include <stddef.h>
#include <librustyaxe/core.h>
#define RR_LINK_SUBPROTOCOL "rustyrig-link.v1"
#define RR_LINK_MAX_FRAME 32768
/* Flat JSON string fields, operation-specific whitelist, bounded messages. */
dict *rr_link_wire_decode(const char *data, size_t length);
char *rr_link_wire_encode(dict *message);
#endif
