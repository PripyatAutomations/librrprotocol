// Compact RustyRig wire boundary. Application events retain their own keys.
#ifndef RR_PROTOCOL_WIRE_H
#define RR_PROTOCOL_WIRE_H
#include <librustyaxe/core.h>
#include <librrprotocol/wire-version.h>

/* Single-version transport codec; internal event dictionaries are not wire JSON.
 * Returned strings/dictionaries are owned by the caller. NULL means invalid
 * input, unsupported operation, exceeded limits or allocation failure.
 * PARITY: rustyrig-www/js/webui.wire.js */
char *rr_wire_encode(dict *message);
dict *rr_wire_decode(const char *json);
#endif
