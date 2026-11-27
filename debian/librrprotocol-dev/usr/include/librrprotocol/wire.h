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
/* Top incoming JSON operations, bytes/rejections/parser time; five-minute limit. Compiles to a no-op without USE_PROFILING. Does not include handler or binary
 * media time. */
void rr_wire_profile_dump(void);
typedef struct rr_wire_profile_summary {
   uint64_t messages, bytes, rejected, processing_us;
   uint64_t json_messages, binary_messages;
} rr_wire_profile_summary_t;
/* Thread-safe cumulative process-local snapshot; false when profiling is off. */
bool rr_wire_profile_summary(rr_wire_profile_summary_t *out);
/* Incoming server binary frames; elapsed includes routing/handler work. */
void rr_wire_profile_binary(const void *data, size_t length, bool accepted, uint64_t elapsed_us);
#endif
