/* RustyRig RTP chunk profile. No socket, UI, or Mongoose dependencies. */
#ifndef RR_RTP_H
#define RR_RTP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RR_RTP_KEY_LEN 30
#define RR_RTP_MTU 1200
#define RR_RTP_MAX_FRAME 65535
#define RR_RTP_CHUNK 1152
typedef struct rr_rtp rr_rtp_t;
typedef bool (*rr_rtp_send_fn)(const uint8_t *, size_t, void *);
rr_rtp_t *rr_rtp_new(const uint8_t key[RR_RTP_KEY_LEN], uint32_t ssrc, bool server, bool encrypted);
void rr_rtp_free(rr_rtp_t *peer);
bool rr_rtp_supported(bool encrypted);
/* RTP/AVP dynamic PT 96, 1 MHz clock, RR extension 0x5252. */
bool rr_rtp_send(rr_rtp_t *peer, const void *frame, size_t length, uint64_t time_us, rr_rtp_send_fn send, void *user);
bool rr_rtp_probe(rr_rtp_t *peer, rr_rtp_send_fn send, void *user);
/* Returns 1 for a complete frame, 2 for authenticated RTCP, 0 for a partial frame, and -1 for malformed, unauthenticated, replayed or late packets. Returned
 * frame memory belongs to peer until its next receive call. */
int rr_rtp_receive(rr_rtp_t *peer, uint8_t *packet, size_t length, uint64_t now_us, const uint8_t **frame, size_t *frame_length);
#endif
