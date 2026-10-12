#include "rtp.socket.h"
#include "build_config.h"
#include <stdlib.h>
#include <string.h>
#if defined(HOST_POSIX) && defined(RR_HAVE_SRTP)
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#define RR_RTP_ATTACHMENTS 128
struct rr_rtp_attachment {
   struct rr_rtp_socket *socket;
   rr_rtp_t *rtp;
   uint32_t ssrc;
   struct sockaddr_storage address;
   socklen_t address_length;
   uint64_t last_seen, last_probe;
   void *user;
   bool client;
};
struct rr_rtp_socket {
   int fd;
   rr_rtp_source_fn source_filter;
   void *filter_context;
   struct rr_rtp_attachment peers[RR_RTP_ATTACHMENTS];
};
void rr_rtp_socket_set_source_filter(rr_rtp_socket_t *s, rr_rtp_source_fn filter, void *context) {
   if (s) {
      s->source_filter = filter;
      s->filter_context = context;
   }
}
static bool address(const char *host, unsigned port, struct sockaddr_storage *out, socklen_t *length) {
   char service[16];
   snprintf(service, sizeof(service), "%u", port);
   struct addrinfo hints = {
      .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM
   }, *result = NULL;

   if (!host || !port || port > 65535 || getaddrinfo(host, service, &hints, &result)) {
      return false;
   }
   memcpy(out, result->ai_addr, result->ai_addrlen);
   *length = result->ai_addrlen;
   freeaddrinfo(result);

   return true;
}
rr_rtp_socket_t *rr_rtp_socket_open(const char *host, unsigned port) {
   rr_rtp_socket_t *s = calloc(1, sizeof(*s));

   if (!s) {
      return NULL;
   }
   s->fd = socket(AF_INET, SOCK_DGRAM, 0);

   if (s->fd < 0) {
      free(s);

      return NULL;
   }
   struct sockaddr_storage addr = {
      0
   };
   socklen_t length = sizeof(struct sockaddr_in);

   if (port) {
      if (!address(host, port, &addr, &length)) {
         rr_rtp_socket_close(s);

         return NULL;
      }
   } else {
      ((struct sockaddr_in *)&addr)->sin_family = AF_INET;
   }

   if (bind(s->fd, (struct sockaddr *)&addr, length) || fcntl(s->fd, F_SETFL, O_NONBLOCK) < 0) {
      rr_rtp_socket_close(s);

      return NULL;
   }

   return s;
}
void rr_rtp_socket_detach(rr_rtp_attachment_t *peer) {
   if (!peer) {
      return;
   }
   rr_rtp_free(peer->rtp);
   memset(peer, 0, sizeof(*peer));
}
void rr_rtp_socket_close(rr_rtp_socket_t *s) {
   if (!s) {
      return;
   }

   for (unsigned i = 0 ; i < RR_RTP_ATTACHMENTS ; i++) {
      rr_rtp_socket_detach(&s->peers[i]);
   }

   close(s->fd);
   free(s);
}
rr_rtp_attachment_t *rr_rtp_socket_attach(rr_rtp_socket_t *s, uint32_t ssrc, const uint8_t key[RR_RTP_KEY_LEN], bool encrypted, const char *endpoint, void *user
   ) {
   if (!s) {
      return NULL;
   }

   for (unsigned i = 0 ; i < RR_RTP_ATTACHMENTS ; i++) {
      if (s->peers[i].rtp && s->peers[i].ssrc == ssrc) {
         return NULL;
      }
   }

   for (unsigned i = 0 ; i < RR_RTP_ATTACHMENTS ; i++) {
      struct rr_rtp_attachment *p = &s->peers[i];

      if (p->rtp) {
         continue;
      }

      if (endpoint) {
         char host[256], extra;
         unsigned port;

         if (sscanf(endpoint, "rtp://%255[^:]:%u%c", host, &port, &extra) != 2 || !address(host, port, &p->address, &p->address_length)) {
            return NULL;
         }
      }
      p->rtp = rr_rtp_new(key, ssrc, endpoint == NULL, encrypted);

      if (!p->rtp) {
         memset(p, 0, sizeof(*p));

         return NULL;
      }
      p->socket = s;
      p->ssrc = ssrc;
      p->client = endpoint != NULL;
      p->user = user;

      return p;
   }

   return NULL;
}
static bool send_packet(const uint8_t *packet, size_t length, void *user) {
   struct rr_rtp_attachment *p = user;

   return p->address_length && sendto(p->socket->fd, packet, length, MSG_DONTWAIT, (struct sockaddr *)&p->address, p->address_length) == (ssize_t) length;
}
bool rr_rtp_socket_active(const rr_rtp_attachment_t *p, uint64_t now) {
   return p && p->rtp && p->last_seen && now >= p->last_seen && now - p->last_seen < 3000000;
}
bool rr_rtp_socket_send(rr_rtp_attachment_t *p, const void *frame, size_t length, uint64_t now) {
   return rr_rtp_socket_active(p, now) && rr_rtp_send(p->rtp, frame, length, now, send_packet, p);
}
void rr_rtp_socket_poll(rr_rtp_socket_t *s, uint64_t now, bool (*validate) (void *), rr_rtp_frame_fn frame) {
   if (!s) {
      return;
   }

   for (unsigned i = 0 ; i < RR_RTP_ATTACHMENTS ; i++) {
      struct rr_rtp_attachment *p = &s->peers[i];

      if (!p->rtp) {
         continue;
      }

      if (validate && !validate(p->user)) {
         rr_rtp_socket_detach(p);
         continue;
      }

      if (p->client && (!p->last_probe || now - p->last_probe >= 1000000)) {
         rr_rtp_probe(p->rtp, send_packet, p);
         p->last_probe = now;
      }
   }

   for (unsigned budget = 0 ; budget < 128 ; budget++) {
      uint8_t packet[RR_RTP_MTU + 1];
      struct sockaddr_storage from = {
         0
      };
      socklen_t length = sizeof(from);
      ssize_t size = recvfrom(s->fd, packet, sizeof(packet), MSG_DONTWAIT, (struct sockaddr *)&from, &length);

      if (size < 0) {
         break;
      }

      if (size < 12 || size > RR_RTP_MTU) {
         continue;
      }
      char source_ip[128];
      if (getnameinfo((struct sockaddr *)&from, length, source_ip, sizeof(source_ip), NULL, 0, NI_NUMERICHOST)) continue;
      if (s->source_filter && !s->source_filter(s->filter_context, NULL, source_ip)) continue;
      unsigned offset = packet[1] == 201 ? 4 : 8;
      uint32_t ssrc = (uint32_t) packet[offset] << 24 | (uint32_t) packet[offset + 1] << 16 | (uint32_t) packet[offset + 2] << 8 | packet[offset + 3];

      for (unsigned i = 0 ; i < RR_RTP_ATTACHMENTS ; i++) {
         struct rr_rtp_attachment *p = &s->peers[i];

         if (!p->rtp || ssrc != (p->client ? p->ssrc : p->ssrc ^ UINT32_C(0x80000000))) {
            continue;
         }

         if (p->client && (length != p->address_length || memcmp(&from, &p->address, length))) {
            break;
         }

         if (!p->client && p->address_length && packet[1] != 201 && (length != p->address_length || memcmp(&from, &p->address, length))) {
            break;
         }
         if (s->source_filter && !s->source_filter(s->filter_context, p->user, source_ip)) break;
         const uint8_t *data = NULL;
         size_t data_length = 0;
         int result = rr_rtp_receive(p->rtp, packet, size, now, &data, &data_length);

         if (result < 0) {
            break;
         }
         p->last_seen = now;

         if (result == 2) {
            if (!p->client) {
               p->address = from;
               p->address_length = length;
               rr_rtp_probe(p->rtp, send_packet, p);
            }

            if (frame) {
               frame(p->user, NULL, 0);
            }
         } else if (result == 1 && frame) {
            frame(p->user, data, data_length);
         }
         break;
      }
   }
}
#else
void rr_rtp_socket_set_source_filter(rr_rtp_socket_t *s, rr_rtp_source_fn filter, void *context) {
   (void)s;
   (void)filter;
   (void)context;
}
rr_rtp_socket_t *rr_rtp_socket_open(const char *host, unsigned port) {
   (void)host;
   (void)port;

   return NULL;
}
void rr_rtp_socket_close(rr_rtp_socket_t *s) {
   (void)s;
}
rr_rtp_attachment_t *rr_rtp_socket_attach(rr_rtp_socket_t *s, uint32_t ssrc, const uint8_t key[RR_RTP_KEY_LEN], bool encrypted, const char *endpoint, void *user
   ) {
   (void)s;
   (void)ssrc;
   (void)key;
   (void)encrypted;
   (void)endpoint;
   (void)user;

   return NULL;
}
void rr_rtp_socket_detach(rr_rtp_attachment_t *p) {
   (void)p;
}
bool rr_rtp_socket_active(const rr_rtp_attachment_t *p, uint64_t now) {
   (void)p;
   (void)now;

   return false;
}
bool rr_rtp_socket_send(rr_rtp_attachment_t *p, const void *frame, size_t length, uint64_t now) {
   (void)p;
   (void)frame;
   (void)length;
   (void)now;

   return false;
}
void rr_rtp_socket_poll(rr_rtp_socket_t *s, uint64_t now, bool (*validate) (void *), rr_rtp_frame_fn frame) {
   (void)s;
   (void)now;
   (void)validate;
   (void)frame;
}
#endif
