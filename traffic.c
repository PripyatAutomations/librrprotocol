#include <stdio.h>
#include <stdint.h>
#include <librrprotocol/traffic.h>

void rr_traffic_count(rrconn_t *peer, bool tx, bool binary, size_t bytes) {
   if (!peer) return;
   if (!peer->traffic_started_us) peer->traffic_started_us = mono_us();
   uint64_t *octets, *frames;
   if (tx && binary) { octets = &peer->traffic.tx_binary_bytes; frames = &peer->traffic.tx_binary_frames; }
   else if (tx) { octets = &peer->traffic.tx_text_bytes; frames = &peer->traffic.tx_text_frames; }
   else if (binary) { octets = &peer->traffic.rx_binary_bytes; frames = &peer->traffic.rx_binary_frames; }
   else { octets = &peer->traffic.rx_text_bytes; frames = &peer->traffic.rx_text_frames; }
   *octets = UINT64_MAX - *octets < bytes ? UINT64_MAX : *octets + bytes;
   if (*frames < UINT64_MAX) (*frames)++;
   size_t *total_bytes = tx ? &peer->tx_bytes : &peer->rx_bytes;
   size_t *total_frames = tx ? &peer->tx_packets : &peer->rx_packets;
   *total_bytes = SIZE_MAX - *total_bytes < bytes ? SIZE_MAX : *total_bytes + bytes;
   if (*total_frames < SIZE_MAX) (*total_frames)++;
}

void rr_traffic_to_dict(dict *out, const char *prefix, const struct rr_traffic *traffic) {
   const char *names[] = {"tx-text-bytes", "tx-text-frames", "tx-binary-bytes", "tx-binary-frames",
      "rx-text-bytes", "rx-text-frames", "rx-binary-bytes", "rx-binary-frames"};
   const uint64_t values[] = {traffic->tx_text_bytes, traffic->tx_text_frames, traffic->tx_binary_bytes, traffic->tx_binary_frames,
      traffic->rx_text_bytes, traffic->rx_text_frames, traffic->rx_binary_bytes, traffic->rx_binary_frames};
   for (unsigned i = 0; i < 8; i++) {
      char key[128], value[32];
      snprintf(key, sizeof(key), "%s.%s", prefix, names[i]);
      snprintf(value, sizeof(value), "%llu", (unsigned long long)values[i]);
      dict_add(out, key, value);
   }
}
