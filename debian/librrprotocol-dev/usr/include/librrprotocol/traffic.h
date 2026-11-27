//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RR_PROTOCOL_TRAFFIC_H
#define RR_PROTOCOL_TRAFFIC_H
#include <librustyaxe/core.h>
/* TX counts accepted enqueue, RX counts complete received application frames, including malformed payloads. Transport headers/retransmissions are excluded. */
void rr_traffic_count(rrconn_t *peer, bool tx, bool binary, size_t bytes);
void rr_traffic_to_dict(dict *out, const char *prefix, const struct rr_traffic *traffic);
#endif
