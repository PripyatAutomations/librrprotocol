//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include "rtp.h"
#include <stdlib.h>
#include <string.h>
#ifdef RR_HAVE_SRTP
#include <srtp2/srtp.h>
struct rr_rtp {
   srtp_t tx, rx;
   uint32_t ssrc, frame_seq, incoming, timestamp;
   uint16_t sequence;
   uint64_t pieces, started;
   uint8_t count;
   size_t length;
   uint8_t *frame;
   bool have_incoming, complete;
};
static uint32_t read32(const uint8_t *p) {
   return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}
static void write32(uint8_t *p, uint32_t n) {
   p[0] = n >> 24;
   p[1] = n >> 16;
   p[2] = n >> 8;
   p[3] = n;
}
bool rr_rtp_supported(bool encrypted) {
#ifdef RR_RTP_ENCRYPTION
   (void)encrypted;

   return true;
#else

   return !encrypted;
#endif
}
rr_rtp_t *rr_rtp_new(const uint8_t key[RR_RTP_KEY_LEN], uint32_t ssrc, bool server, bool encrypted) {
   static bool initialized;

   if (!key || !ssrc || !rr_rtp_supported(encrypted)) {
      return NULL;
   }

   if (!initialized) {
      if (srtp_init() != srtp_err_status_ok) {
         return NULL;
      }
      initialized = true;
   }
   rr_rtp_t *peer = calloc(1, sizeof(*peer));

   if (!peer) {
      return NULL;
   }
   peer->ssrc = server ? ssrc : ssrc ^ UINT32_C(0x80000000);

   for (unsigned direction = 0 ; direction < 2 ; direction++) {
      srtp_policy_t policy = {
         0
      };
#ifdef RR_RTP_ENCRYPTION

      if (encrypted) {
         srtp_crypto_policy_set_rtp_default(&policy.rtp);
         srtp_crypto_policy_set_rtcp_default(&policy.rtcp);
      } else
#endif
      {
         srtp_crypto_policy_set_null_cipher_hmac_sha1_80(&policy.rtp);
         srtp_crypto_policy_set_null_cipher_hmac_sha1_80(&policy.rtcp);
      }
      policy.ssrc.type = ssrc_specific;
      policy.ssrc.value = direction ? peer->ssrc ^ UINT32_C(0x80000000) : peer->ssrc;
      policy.key = (uint8_t *) key;
      policy.window_size = 128;

      if (srtp_create(direction ? &peer->rx : &peer->tx, &policy) != srtp_err_status_ok) {
         rr_rtp_free(peer);

         return NULL;
      }
   }

   return peer;
}
void rr_rtp_free(rr_rtp_t *peer) {
   if (!peer) {
      return;
   }

   if (peer->tx) {
      srtp_dealloc(peer->tx);
   }

   if (peer->rx) {
      srtp_dealloc(peer->rx);
   }
   free(peer->frame);
   memset(peer, 0, sizeof(*peer));
   free(peer);
}
bool rr_rtp_probe(rr_rtp_t *peer, rr_rtp_send_fn send, void *user) {
   if (!peer || !send) {
      return false;
   }
   uint8_t packet[64] = {
      0x80, 201, 0, 1
   };                           /* RTCP receiver report, no report blocks. */
   write32(packet + 4, peer->ssrc);
   int length = 8;

   return srtp_protect_rtcp(peer->tx, packet, &length) == srtp_err_status_ok && send(packet, length, user);
}
bool rr_rtp_send(rr_rtp_t *peer, const void *frame, size_t length, uint64_t time_us, rr_rtp_send_fn send, void *user) {
   if (!peer || !frame || !length || length > RR_RTP_MAX_FRAME || !send) {
      return false;
   }
   unsigned count = (length + RR_RTP_CHUNK - 1) / RR_RTP_CHUNK;

   if (++peer->frame_seq == 0) {
      return false;             /* Rotate attachment before counter reuse. */
   }

   for (unsigned i = 0 ; i < count ; i++) {
      uint8_t packet[RR_RTP_MTU] = {
         0x90, 96
      };
      packet[1] |= i + 1 == count ? 0x80 : 0;
      uint16_t sequence = peer->sequence++;
      packet[2] = sequence >> 8;
      packet[3] = sequence;
      write32(packet + 4, time_us);
      write32(packet + 8, peer->ssrc);
      packet[12] = 'R';
      packet[13] = 'R';
      packet[15] = 2;
      write32(packet + 16, peer->frame_seq);
      packet[20] = i;
      packet[21] = count;
      packet[22] = length >> 8;
      packet[23] = length;
      size_t part = length - i * RR_RTP_CHUNK;

      if (part > RR_RTP_CHUNK) {
         part = RR_RTP_CHUNK;
      }
      memcpy(packet + 24, (const uint8_t *)frame + i * RR_RTP_CHUNK, part);
      int size = 24 + part;

      if (srtp_protect(peer->tx, packet, &size) != srtp_err_status_ok || !send(packet, size, user)) {
         return false;
      }
   }

   return true;
}
int rr_rtp_receive(rr_rtp_t *peer, uint8_t *packet, size_t length, uint64_t now_us, const uint8_t **frame, size_t *frame_length) {
   if (!peer || !packet || !frame || !frame_length || length < 8 || length > RR_RTP_MTU) {
      return -1;
   }
   int size = length;

   if (packet[1] == 201) {
      return srtp_unprotect_rtcp(peer->rx, packet, &size) == srtp_err_status_ok && size == 8 && packet[0] == 0x80 && packet[2] == 0 && packet[3] == 1 ? 2 : -1;
   }

   if (srtp_unprotect(peer->rx, packet, &size) != srtp_err_status_ok || size <= 24 ||
      packet[0] != 0x90 || (packet[1] & 0x7f) != 96 || packet[12] != 'R' || packet[13] != 'R' || packet[14] != 0 || packet[15] != 2) {
      return -1;
   }
   uint32_t sequence = read32(packet + 16), timestamp = read32(packet + 4);
   unsigned index = packet[20], count = packet[21];
   size_t total = (size_t)packet[22] << 8 | packet[23];

   if (!total || !count || count > 64 || index >= count || count != (total + RR_RTP_CHUNK - 1) / RR_RTP_CHUNK) {
      return -1;
   }
   size_t part = total - index * RR_RTP_CHUNK;

   if (part > RR_RTP_CHUNK) {
      part = RR_RTP_CHUNK;
   }

   if ((size_t)size != 24 + part) {
      return -1;
   }

   if (peer->have_incoming && sequence != peer->incoming && (int32_t) (sequence - peer->incoming) <= 0) {
      return -1;
   }

   if (!peer->have_incoming || sequence != peer->incoming) {
      uint8_t *buffer = realloc(peer->frame, total);

      if (!buffer) {
         return -1;
      }
      peer->frame = buffer;
      peer->incoming = sequence;
      peer->timestamp = timestamp;
      peer->length = total;
      peer->count = count;
      peer->pieces = 0;
      peer->started = now_us;
      peer->complete = false;
      peer->have_incoming = true;
   }

   if (peer->complete || now_us - peer->started > 100000 || peer->timestamp != timestamp ||
      peer->length != total || peer->count != count || (peer->pieces & (UINT64_C(1) << index))) {
      return -1;
   }
   memcpy(peer->frame + index * RR_RTP_CHUNK, packet + 24, part);
   peer->pieces |= UINT64_C(1) << index;

   if (peer->pieces != (count == 64 ? UINT64_MAX : (UINT64_C(1) << count) - 1)) {
      return 0;
   }
   peer->complete = true;
   *frame = peer->frame;
   *frame_length = peer->length;

   return 1;
}
#else
bool rr_rtp_supported(bool encrypted) {
   (void)encrypted;

   return false;
}
rr_rtp_t *rr_rtp_new(const uint8_t key[RR_RTP_KEY_LEN], uint32_t ssrc, bool server, bool encrypted) {
   (void)key;
   (void)ssrc;
   (void)server;
   (void)encrypted;

   return NULL;
}
void rr_rtp_free(rr_rtp_t *peer) {
   (void)peer;
}
bool rr_rtp_send(rr_rtp_t *peer, const void *frame, size_t length, uint64_t time_us, rr_rtp_send_fn send, void *user) {
   (void)peer;
   (void)frame;
   (void)length;
   (void)time_us;
   (void)send;
   (void)user;

   return false;
}
bool rr_rtp_probe(rr_rtp_t *peer, rr_rtp_send_fn send, void *user) {
   (void)peer;
   (void)send;
   (void)user;

   return false;
}
int rr_rtp_receive(rr_rtp_t *peer, uint8_t *packet, size_t length, uint64_t now_us, const uint8_t **frame, size_t *frame_length) {
   (void)peer;
   (void)packet;
   (void)length;
   (void)now_us;
   (void)frame;
   (void)frame_length;

   return -1;
}
#endif
