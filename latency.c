// librrprotcol/latency.c: Latency estimation tools
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// We handle estimating RTT via all ACKed commands for ms level estimation
// We handle keep-alives with microsecond resolution, if ntp is available on both ends
#include <string.h>
#include <stdio.h>
#include <librrprotocol/latency.h>
#include <librrprotocol/media.health.h>

void rr_latency_sent(rrconn_t *peer, dict *message, uint64_t sent_us) {
   if (!peer || !message || !sent_us) {
      return;
   }

   const char *id = dict_get(message, "request.id", NULL);

   if (!id || !*id || strlen(id) >= sizeof(peer->latency_request)) {
      return;
   }

   if (peer->latency_sent_us && sent_us >= peer->latency_sent_us && sent_us - peer->latency_sent_us < 5000000) {
      return;
   }
   snprintf(peer->latency_request, sizeof(peer->latency_request), "%s", id);
   peer->latency_sent_us = sent_us;
}

bool rr_latency_received(rrconn_t *peer, dict *message, uint64_t received_us) {
   if (!peer || !message || !peer->latency_sent_us || received_us < peer->latency_sent_us) {
      return false;
   }

   const char *id = dict_get(message, "request.id", NULL);

   if (!id || strcmp(id, peer->latency_request)) {
      return false;
   }

   peer->response_rtt_us = received_us - peer->latency_sent_us;
   rr_media_rtt_sample(peer, peer->response_rtt_us);
   peer->latency_sent_us = 0;
   peer->latency_request[0] = 0;

   return true;
}
