//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Compact RustyRig wire boundary. Application events retain their own keys.
#ifndef RR_PROTOCOL_WIRE_H
#define RR_PROTOCOL_WIRE_H
#include <librustyaxe/core.h>
#include <librrprotocol/wire-version.h>

/* Single-version transport codec; internal event dictionaries are not wire JSON. Returned strings/dictionaries are owned by the caller. NULL means invalid
 * input, unsupported operation, exceeded limits or allocation failure. PARITY: rustyrig-www/js/webui.wire.js */
char *rr_wire_encode(dict *message);
dict *rr_wire_decode(const char *json);
#endif
