#ifndef RR_MEDIA_HEALTH_H
#define RR_MEDIA_HEALTH_H
#include <librustyaxe/core.h>
#include <librrprotocol/ws.binframe.h>
/* PARITY: rustyrig-www/js/webui.media.health.js. Local monotonic times only. */
uint32_t rr_media_next_sequence(rrconn_t *peer, const struct rr_binframe_hdr *frame);
void rr_media_rtt_sample(rrconn_t *peer, uint64_t rtt_us);
unsigned rr_media_quality(rrconn_t *peer, const struct rr_binframe_hdr *frame, size_t queued, size_t limit, uint64_t at, bool *notify);
unsigned rr_media_flow_quality(rrconn_t *peer, uint8_t stream, uint8_t direction, const char codec[4], uint64_t at);
/* False rejects a duplicate/reordered frame. Gaps flag discontinuity, not TCP loss. */
bool rr_media_observe(rrconn_t *peer, const struct rr_binframe_hdr *frame, uint64_t at, bool *discontinuity, dict **feedback);
bool rr_media_feedback(rrconn_t *peer, dict *message, uint8_t stream, uint8_t direction, const char codec[4], uint64_t at);
#endif
