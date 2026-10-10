#include <string.h>
#include <limits.h>
#include <librrprotocol/media.health.h>

static struct rr_media_flow *flow_for(rrconn_t *peer, uint8_t stream, uint8_t direction, const char codec[4]) {
   struct rr_media_flow *oldest = &peer->media_flows[0];
   for (unsigned i = 0; i < RR_MEDIA_FLOW_SLOTS; i++) {
      struct rr_media_flow *flow = &peer->media_flows[i];
      if (flow->stream == stream && flow->direction == direction && !memcmp(flow->codec, codec, 4)) { flow->used_at = mono_us(); return flow; }
      if (!flow->stream || flow->used_at < oldest->used_at) oldest = flow;
   }
   memset(oldest, 0, sizeof(*oldest));
   oldest->stream = stream;
   oldest->direction = direction;
   memcpy(oldest->codec, codec, 4);
   oldest->quality = 100;
   oldest->used_at = mono_us();
   return oldest;
}

uint32_t rr_media_next_sequence(rrconn_t *peer, const struct rr_binframe_hdr *frame) {
   return peer->media_tx_sequences[frame->stream]++;
}

void rr_media_rtt_sample(rrconn_t *peer, uint64_t rtt_us) {
   if (!peer || !rtt_us || rtt_us > 60000000) return;
   if (!peer->media_rtt_base_us || rtt_us < peer->media_rtt_base_us) peer->media_rtt_base_us = rtt_us;
   peer->media_rtt_smoothed_us = peer->media_rtt_smoothed_us ?
      peer->media_rtt_smoothed_us - peer->media_rtt_smoothed_us / 8 + rtt_us / 8 : rtt_us;
}

unsigned rr_media_flow_quality(rrconn_t *peer, uint8_t stream, uint8_t direction, const char codec[4], uint64_t at) {
   (void)at;
   if (!peer) return 100;
   struct rr_media_flow *flow = flow_for(peer, stream, direction, codec);
   return flow->quality ? flow->quality : 100;
}

unsigned rr_media_quality(rrconn_t *peer, const struct rr_binframe_hdr *frame, size_t queued, size_t limit, uint64_t at, bool *notify) {
   *notify = false;
   struct rr_media_flow *flow = flow_for(peer, frame->stream, frame->direction, frame->codec);
   unsigned desired = queued >= limit / 2 ? 50 : queued >= limit / 4 ? 75 : 100;
   uint64_t base = peer->media_rtt_base_us, smooth = peer->media_rtt_smoothed_us;
   uint64_t soft = base / 2 > 50000 ? base / 2 : 50000;
   uint64_t hard = base > 150000 ? base : 150000;
   if (base && smooth > base && smooth - base > soft) {
      if (!flow->tx_bad_since) flow->tx_bad_since = at;
      if (at >= flow->tx_bad_since && at - flow->tx_bad_since >= 500000) {
         unsigned latency_quality = smooth - base > hard ? 50 : 75;
         if (latency_quality < desired) desired = latency_quality;
      }
   } else flow->tx_bad_since = 0;
   if (flow->remote_at && at >= flow->remote_at && at - flow->remote_at < 10000000 && flow->remote_quality < desired)
      desired = flow->remote_quality;
   if (desired < flow->quality) {
      flow->quality = desired;
      flow->healthy_since = 0;
      *notify = true;
   } else if (desired > flow->quality) {
      if (!flow->healthy_since) flow->healthy_since = at;
      if (at >= flow->healthy_since && at - flow->healthy_since >= 5000000) {
         flow->quality += 25;
         if (flow->quality > desired) flow->quality = desired;
         flow->healthy_since = at;
         *notify = true;
      }
   } else flow->healthy_since = 0;
   if (!flow->hint_at || at < flow->hint_at || at - flow->hint_at >= 5000000) *notify = true;
   if (*notify) flow->hint_at = at;
   peer->media_quality = flow->quality;
   return flow->quality;
}

bool rr_media_feedback(rrconn_t *peer, dict *message, uint8_t stream, uint8_t direction, const char codec[4], uint64_t at) {
   if (!peer || !message || dict_get_type(message, "media.quality") != VAL_INT ||
      dict_get_type(message, "media.stream") != VAL_INT || dict_get_type(message, "media.dir") != VAL_INT ||
      dict_get_long(message, "media.stream", -1) != stream || dict_get_long(message, "media.dir", -1) != direction ||
      strcmp(dict_get(message, "media.codec", ""), (char [5]){codec[0],codec[1],codec[2],codec[3],0})) return false;
   long quality = dict_get_long(message, "media.quality", 0);
   if (quality != 50 && quality != 75 && quality != 100) return false;
   struct rr_media_flow *flow = flow_for(peer, stream, direction, codec);
   flow->remote_quality = quality;
   flow->remote_at = at;
   return true;
}

bool rr_media_observe(rrconn_t *peer, const struct rr_binframe_hdr *frame, uint64_t at, bool *discontinuity, dict **feedback) {
   *feedback = NULL;
   *discontinuity = false;
   if (!peer || !frame || !frame->stream) return false;
   struct rr_media_flow *flow = flow_for(peer, frame->stream, frame->direction, frame->codec);
   if (flow->last_arrival && at >= flow->last_arrival) {
      uint32_t advance = frame->seq - flow->sequence;
      if (!advance || advance > INT32_MAX) return false;
   }
   bool reset = !flow->last_arrival || at < flow->last_arrival ||
      (frame->ts && frame->ts < flow->last_source);
   if (!reset) {
      uint32_t advance = frame->seq - flow->sequence;
      if (!advance || advance > INT32_MAX) return false;
      if (advance > 1) {
         flow->gaps += advance - 1 > 10000 ? 10000 : advance - 1;
         if (flow->gaps > 10000) flow->gaps = 10000;
         flow->last_bad = at;
         *discontinuity = true;
      }
   } else {
      *discontinuity = flow->last_arrival != 0;
      flow->source_origin = frame->ts;
      flow->arrival_origin = at;
      flow->rx_bad_since = flow->last_bad = 0;
   }
   uint64_t excess = 0;
   if (frame->ts && frame->ts >= flow->source_origin && at >= flow->arrival_origin) {
      uint64_t sent = frame->ts - flow->source_origin, elapsed = at - flow->arrival_origin;
      if (elapsed < sent) {
         flow->source_origin = frame->ts;
         flow->arrival_origin = at;
      } else excess = elapsed - sent;
   }
   if (excess >= 50000) {
      if (!flow->rx_bad_since) flow->rx_bad_since = at;
      flow->last_bad = at;
      if (excess >= 250000) *discontinuity = true;
   } else flow->rx_bad_since = 0;
   flow->sequence = frame->seq;
   flow->last_source = frame->ts;
   flow->last_arrival = at;
   if (!flow->report_at || at < flow->report_at || at - flow->report_at >= 1000000) {
      unsigned quality = 100;
      if (flow->last_bad && at >= flow->last_bad && at - flow->last_bad < 1000000) quality = 75;
      if (flow->rx_bad_since && at >= flow->rx_bad_since && at - flow->rx_bad_since >= 500000 && excess >= 150000) quality = 50;
      dict *report = dict_new();
      if (report) {
         char codec[5]; memcpy(codec, frame->codec, 4); codec[4] = 0;
         dict_add(report, "msg.type", "media");
         dict_add(report, "media.cmd", "feedback");
         dict_add_uint(report, "media.stream", frame->stream);
         dict_add_uint(report, "media.dir", frame->direction);
         dict_add(report, "media.codec", codec);
         dict_add_uint(report, "media.quality", quality);
         dict_add_llong(report, "media.late-us", excess > 60000000 ? 60000000 : excess);
         dict_add_uint(report, "media.gaps", flow->gaps);
         *feedback = report;
         flow->report_at = at;
         flow->gaps = 0;
      }
   }
   return true;
}
