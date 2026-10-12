#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "librrprotocol/rtp.h"
struct capture {
   uint8_t data[64][RR_RTP_MTU];
   size_t sizes[64], count;
};
static bool capture(const uint8_t *packet, size_t length, void *user) {
   struct capture *c = user;
   assert(c->count < 64 && length <= RR_RTP_MTU);
   memcpy(c->data[c->count], packet, length);
   c->sizes[c->count++] = length;

   return true;
}
int main(int argc, char **argv) {
   bool encrypted = argc > 1 && atoi(argv[1]);
   uint8_t key[RR_RTP_KEY_LEN] = {
      1, 2, 3
   };
   rr_rtp_t *server = rr_rtp_new(key, 42, true, encrypted), *client = rr_rtp_new(key, 42, false, encrypted);
   assert(server && client);
   struct capture c = {
      0
   };
   assert(rr_rtp_probe(client, capture, &c));
   const uint8_t *frame = NULL;
   size_t length = 0;
   uint8_t replay[RR_RTP_MTU];
   memcpy(replay, c.data[0], c.sizes[0]);
   assert(rr_rtp_receive(server, c.data[0], c.sizes[0], 1, &frame, &length) == 2);
   assert(rr_rtp_receive(server, replay, c.sizes[0], 2, &frame, &length) == -1);
   c.count = 0;
   uint8_t source[RR_RTP_MAX_FRAME];

   for (size_t i = 0 ; i < sizeof(source) ; i++) {
      source[i] = i;
   }

   assert(rr_rtp_send(server, source, sizeof(source), 12345, capture, &c));

   /* Reordering within one frame is allowed; complete frames remain ordered. */
   for (size_t i = c.count ; i-- > 0 ; ) {
      assert(rr_rtp_receive(client, c.data[i], c.sizes[i], 100 + c.count - i, &frame, &length) == (i == 0 ? 1 : 0));
   }

   assert(length == sizeof(source) && !memcmp(source, frame, length));
   c.count = 0;
   assert(rr_rtp_send(server, source, 2000, 12346, capture, &c));
   memcpy(replay, c.data[0], c.sizes[0]);
   replay[30] ^= 1;
   assert(rr_rtp_receive(client, replay, c.sizes[0], 1000, &frame, &length) == -1);
   assert(rr_rtp_receive(client, c.data[0], c.sizes[0], 1000, &frame, &length) == 0);
   assert(rr_rtp_receive(client, c.data[1], c.sizes[1], 101001, &frame, &length) == -1);
   c.count = 0;
   assert(rr_rtp_send(client, source, 640, 555, capture, &c));
   assert(rr_rtp_receive(server, c.data[0], c.sizes[0], 1, &frame, &length) == 1);
   assert(length == 640 && !memcmp(source, frame, length));
   /* RTP sequence rollover must not interrupt authenticated microphone audio. */
   for (unsigned i = 0; i < 66000; i++) {
      c.count = 0;
      assert(rr_rtp_send(client, source, 16, 1000 + i, capture, &c));
      assert(rr_rtp_receive(server, c.data[0], c.sizes[0], 1000 + i, &frame, &length) == 1);
      assert(length == 16 && !memcmp(source, frame, length));
   }
   uint8_t other_key[RR_RTP_KEY_LEN] = {9};
   rr_rtp_t *intruder = rr_rtp_new(other_key, 42, false, encrypted);
   c.count = 0;
   assert(rr_rtp_probe(intruder, capture, &c));
   assert(rr_rtp_receive(server, c.data[0], c.sizes[0], 70000, &frame, &length) == -1);
   rr_rtp_free(intruder);
   c.count = 0;
   assert(!rr_rtp_send(server, source, 0, 1, capture, &c));
   assert(!rr_rtp_send(server, source, RR_RTP_MAX_FRAME + 1, 1, capture, &c));
   rr_rtp_free(server);
   rr_rtp_free(client);
   puts("PASS: authenticated bidirectional RTP, bounded fragmentation, reordering, tamper/replay rejection and late-frame discard");
}
