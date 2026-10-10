#ifndef RR_PROTOCOL_LATENCY_H
#define RR_PROTOCOL_LATENCY_H
#include <librustyaxe/core.h>
/* One bounded sample per connection, using the originator's monotonic clock.
 * First correlated reply measures round trip including peer processing. */
void rr_latency_sent(rrconn_t *peer, dict *message, uint64_t sent_us);
bool rr_latency_received(rrconn_t *peer, dict *message, uint64_t received_us);
#endif
