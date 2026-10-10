#include <assert.h>
#include <string.h>
#include <librrprotocol/media.health.h>
#include <librrprotocol/wire.h>
#include <librrprotocol/traffic.h>
int main(void) {
   rrconn_t peer = {0}, other = {0};
   struct rr_binframe_hdr frame = {.stream=1,.direction=0,.ts=100000000,.seq=UINT32_MAX};
   memcpy(frame.codec,"opus",4);
   bool notify, discontinuity;
   dict *feedback;
   rr_media_rtt_sample(&peer,300000);
   assert(rr_media_quality(&peer,&frame,0,8192,1000000,&notify)==100); // naturally high RTT
   assert(rr_media_quality(&peer,&frame,4096,8192,2000000,&notify)==50);
   assert(rr_media_quality(&peer,&frame,0,8192,3000000,&notify)==50);
   assert(rr_media_quality(&peer,&frame,0,8192,8000000,&notify)==75);
   assert(rr_media_quality(&peer,&frame,0,8192,13000000,&notify)==100);
   for (unsigned i=0;i<16;i++) rr_media_rtt_sample(&peer,900000);
   assert(rr_media_quality(&peer,&frame,0,8192,14000000,&notify)==100);
   assert(rr_media_quality(&peer,&frame,0,8192,14600000,&notify)==50);
   assert(rr_media_observe(&other,&frame,1000000,&discontinuity,&feedback) && !discontinuity && feedback);
   char *wire=rr_wire_encode(feedback); assert(wire);
   dict *decoded=rr_wire_decode(wire); assert(decoded);
   assert(rr_media_feedback(&peer,decoded,1,0,"opus",15000000));
   assert(!rr_media_feedback(&peer,decoded,2,0,"opus",15000000));
   dict_free(decoded); dict_free(feedback); free(wire);
   frame.seq=0;frame.ts+=20000;
   assert(rr_media_observe(&other,&frame,1020000,&discontinuity,&feedback) && !discontinuity); // wrap
   assert(!feedback);
   assert(!rr_media_observe(&other,&frame,1020001,&discontinuity,&feedback)); // duplicate
   frame.seq=2;frame.ts+=20000;
   assert(rr_media_observe(&other,&frame,1060000,&discontinuity,&feedback) && discontinuity); // one skipped frame
   frame.seq=1; frame.ts-=20000;
   assert(!rr_media_observe(&other,&frame,1060001,&discontinuity,&feedback));
   frame.seq=3; frame.ts+=40000;
   assert(rr_media_observe(&other,&frame,2000000,&discontinuity,&feedback) && discontinuity && feedback);
   assert(dict_get_uint(feedback,"media.quality",0)==75); dict_free(feedback);
   frame.seq=4;frame.ts+=20000;
   assert(rr_media_observe(&other,&frame,3000000,&discontinuity,&feedback) && feedback);
   assert(dict_get_uint(feedback,"media.quality",0)==50); dict_free(feedback);
   frame.stream=2;
   assert(rr_media_observe(&other,&frame,4000000,&discontinuity,&feedback) && !discontinuity);
   dict_free(feedback);
   frame.stream=1;
   assert(rr_media_next_sequence(&peer,&frame)==0);
   for (unsigned i=2;i<20;i++) { frame.stream=i; rr_media_quality(&peer,&frame,0,8192,5000000,&notify); }
   frame.stream=1;
   assert(rr_media_next_sequence(&peer,&frame)==1); // health-cache eviction must not restart sequence
   rr_traffic_count(&peer,true,false,10);rr_traffic_count(&peer,true,true,20);
   rr_traffic_count(&peer,false,false,30);rr_traffic_count(&peer,false,true,40);
   assert(peer.traffic.tx_text_bytes==10 && peer.traffic.tx_binary_bytes==20);
   assert(peer.traffic.rx_text_frames==1 && peer.traffic.rx_binary_bytes==40);
   assert(!other.traffic.tx_text_frames);
   puts("PASS: relative RTT, sustained congestion, gradual recovery, feedback scope, sequence wrap/gaps and separate traffic counters");
}
