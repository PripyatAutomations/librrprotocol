#ifndef RR_PROTOCOL_TRAFFIC_H
#define RR_PROTOCOL_TRAFFIC_H
#include <librustyaxe/core.h>
/* TX counts accepted enqueue, RX counts complete received application frames,
 * including malformed payloads. Transport headers/retransmissions are excluded. */
void rr_traffic_count(rrconn_t *peer, bool tx, bool binary, size_t bytes);
void rr_traffic_to_dict(dict *out, const char *prefix, const struct rr_traffic *traffic);
#endif
