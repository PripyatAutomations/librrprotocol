#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <librrprotocol/latency.h>
int main(void) {
   rrconn_t peer = {0}, other = {0};
   dict *request = dict_new(), *reply = dict_new();
   dict_add(request, "request.id", "first");
   dict_add(reply, "request.id", "other");
   rr_latency_sent(&peer, request, 1000000);
   assert(!rr_latency_received(&peer, reply, 1000500));
   dict_add(request, "request.id", "second");
   rr_latency_sent(&peer, request, 1001000);
   assert(!strcmp(peer.latency_request, "first")); // bounded outstanding sample
   dict_add(reply, "request.id", "first");
   assert(!rr_latency_received(&other, reply, 1000500));
   assert(!rr_latency_received(&peer, reply, 999999));
   assert(rr_latency_received(&peer, reply, 1000500));
   assert(peer.response_rtt_us == 500 && !peer.latency_sent_us);
   assert(!rr_latency_received(&peer, reply, 1001000)); // duplicate is not a new RTT
   rr_latency_sent(&peer, request, 2000000);
   dict_add(request, "request.id", "replacement");
   rr_latency_sent(&peer, request, 7000000);
   assert(!strcmp(peer.latency_request, "replacement")); // expire unanswered sample
   dict_free(request); dict_free(reply);
   puts("PASS: precise monotonic correlated RTT, bounded sampling and peer isolation");
   return 0;
}
