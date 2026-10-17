//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Bounded local mono PCM transport, independent of bridge control records.
#ifndef RRPROTOCOL_BRIDGE_MEDIA_H
#define RRPROTOCOL_BRIDGE_MEDIA_H
#include <stddef.h>
#include <stdint.h>
#define RRP_BRIDGE_MEDIA_FD 4
#define RRP_BRIDGE_MEDIA_SAMPLES 2048
struct rrp_bridge_pcm {
   unsigned rate;
   size_t count;
   int16_t samples[RRP_BRIDGE_MEDIA_SAMPLES];
};
// Send: 1=sent, 0=dropped under backpressure, -1=invalid/closed.
int rrp_bridge_media_send(int fd, const int16_t *samples, size_t count, unsigned rate);
// Receive: 1=PCM, 0=would block, -1=invalid/closed. Wire samples are S16LE.
int rrp_bridge_media_recv(int fd, struct rrp_bridge_pcm *frame);
#endif
