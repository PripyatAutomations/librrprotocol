#include <string.h>
#include <stdio.h>
#include <librrprotocol/latency.h>

void rr_latency_sent(rrconn_t *peer, dict *message, uint64_t sent_us) {
   if (!peer || !message || !sent_us) return;
   const char *id = dict_get(message, "request.id", NULL);
   if (!id || !*id || strlen(id) >= sizeof(peer->latency_request)) return;
   if (peer->latency_sent_us && sent_us >= peer->latency_sent_us && sent_us - peer->latency_sent_us < 5000000) return;
   snprintf(peer->latency_request, sizeof(peer->latency_request), "%s", id);
   peer->latency_sent_us = sent_us;
}

bool rr_latency_received(rrconn_t *peer, dict *message, uint64_t received_us) {
   if (!peer || !message || !peer->latency_sent_us || received_us < peer->latency_sent_us) return false;
   const char *id = dict_get(message, "request.id", NULL);
   if (!id || strcmp(id, peer->latency_request)) return false;
   peer->response_rtt_us = received_us - peer->latency_sent_us;
   peer->latency_sent_us = 0;
   peer->latency_request[0] = 0;
   return true;
}
