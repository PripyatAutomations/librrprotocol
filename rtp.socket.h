//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RR_RTP_SOCKET_H
#define RR_RTP_SOCKET_H
#include "rtp.h"
typedef struct rr_rtp_socket rr_rtp_socket_t;
typedef struct rr_rtp_attachment rr_rtp_attachment_t;
typedef bool (*rr_rtp_frame_fn)(void *user, const uint8_t *frame, size_t length);
/* Host socket adapter. The packet/protection implementation is separate. */
rr_rtp_socket_t *rr_rtp_socket_open(const char *host, unsigned port);
/* Source policy receives numeric IP and attachment userdata before any packet is accepted. NULL attachment means the initial network-only check. */
typedef bool (*rr_rtp_source_fn)(void *context, void *attachment_user, const char *ip);
void rr_rtp_socket_set_source_filter(rr_rtp_socket_t *socket, rr_rtp_source_fn filter, void *context);
void rr_rtp_socket_close(rr_rtp_socket_t *socket);
rr_rtp_attachment_t *rr_rtp_socket_attach(rr_rtp_socket_t *socket, uint32_t ssrc, const uint8_t key[RR_RTP_KEY_LEN], bool encrypted, const char *endpoint,
   void *user);
void rr_rtp_socket_detach(rr_rtp_attachment_t *attachment);
bool rr_rtp_socket_active(const rr_rtp_attachment_t *attachment, uint64_t now);
bool rr_rtp_socket_send(rr_rtp_attachment_t *attachment, const void *frame, size_t length, uint64_t now);
/* validate is called before accepting packets or sending probes. frame(NULL,0) denotes an authenticated binding/probe, not audio. */
void rr_rtp_socket_poll(rr_rtp_socket_t *socket, uint64_t now, bool (*validate) (void *user), rr_rtp_frame_fn frame);
#endif
