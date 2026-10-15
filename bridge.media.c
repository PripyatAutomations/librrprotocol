#include <librrprotocol/bridge.media.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>

int rrp_bridge_media_send(int fd, const int16_t *samples, size_t count, unsigned rate) {
   if (fd < 0 || !samples || !count || count > RRP_BRIDGE_MEDIA_SAMPLES || rate < 8000 || rate > 48000) return -1;
   uint8_t wire[12 + 2 * RRP_BRIDGE_MEDIA_SAMPLES] = {'R', 'R', 'B', 'A', 1, 1, 0, 0};
   for (int i = 0; i < 4; i++) wire[8 + i] = rate >> (24 - 8 * i);
   for (size_t i = 0; i < count; i++) { wire[12 + 2*i] = (uint16_t)samples[i]; wire[13 + 2*i] = (uint16_t)samples[i] >> 8; }
   ssize_t n;
   do { n = send(fd, wire, 12 + 2*count, MSG_DONTWAIT | MSG_NOSIGNAL); } while (n < 0 && errno == EINTR);
   if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
   return n == (ssize_t)(12 + 2*count) ? 1 : -1;
}
int rrp_bridge_media_recv(int fd, struct rrp_bridge_pcm *frame) {
   if (fd < 0 || !frame) return -1;
   uint8_t wire[12 + 2 * RRP_BRIDGE_MEDIA_SAMPLES];
   ssize_t n = recv(fd, wire, sizeof(wire), MSG_DONTWAIT | MSG_TRUNC);
   if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
   if (n < 14 || n > (ssize_t)sizeof(wire) || (n & 1) || memcmp(wire, "RRBA\1\1\0\0", 8)) return -1;
   frame->rate = ((uint32_t)wire[8] << 24) | ((uint32_t)wire[9] << 16) | ((uint32_t)wire[10] << 8) | wire[11];
   if (frame->rate < 8000 || frame->rate > 48000) return -1;
   frame->count = (n - 12) / 2;
   for (size_t i = 0; i < frame->count; i++) frame->samples[i] = (int16_t)((uint16_t)wire[12 + 2*i] | ((uint16_t)wire[13 + 2*i] << 8));
   return 1;
}
