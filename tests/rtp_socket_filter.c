#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rtp.socket.h>
static bool network, identity;
static unsigned network_calls, identity_calls, bindings;
static int marker;
static bool filter(void *context, void *user, const char *ip) {
   assert(context == &marker && !strcmp(ip, "127.0.0.1"));

   if (!user) {
      network_calls++;

      return network;
   }
   assert(user == &marker);
   identity_calls++;

   return identity;
}
static bool frame(void *user, const uint8_t *data, size_t length) {
   assert(user == &marker && !data && !length);
   bindings++;

   return true;
}
int main(void) {
   if (!rr_rtp_supported(false)) {
      puts("SKIP: RTP transport disabled");

      return 0;
   }
   int fd = socket(AF_INET, SOCK_DGRAM, 0);
   assert(fd >= 0);
   struct sockaddr_in address = {
      .sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
   };
   assert(!bind(fd, (struct sockaddr *)&address, sizeof(address)));
   socklen_t length = sizeof(address);
   assert(!getsockname(fd, (struct sockaddr *)&address, &length));
   close(fd);
   unsigned port = ntohs(address.sin_port);
   rr_rtp_socket_t *server = rr_rtp_socket_open("127.0.0.1", port);
   rr_rtp_socket_t *client = rr_rtp_socket_open("127.0.0.1", 0);
   assert(server && client);
   char endpoint[64];
   snprintf(endpoint, sizeof(endpoint), "rtp://127.0.0.1:%u", port);
   uint8_t key[RR_RTP_KEY_LEN];
   memset(key, 0x47, sizeof(key));
   rr_rtp_attachment_t *owner = rr_rtp_socket_attach(server, 123, key, false, NULL, &marker);
   assert(owner && rr_rtp_socket_attach(client, 123, key, false, endpoint, &marker));
   rr_rtp_socket_set_source_filter(server, filter, &marker);
   uint64_t now = mono_us();

   for (unsigned phase = 0 ; phase < 3 ; phase++) {
      network = phase > 0;
      identity = phase > 1;

      for (unsigned i = 0 ; i < 4 ; i++) {
         now += UINT64_C(1000001);
         rr_rtp_socket_poll(client, now, NULL, NULL);
         rr_rtp_socket_poll(server, now, NULL, frame);
         rr_rtp_socket_poll(client, now, NULL, NULL);
      }

      if (phase == 0) {
         assert(network_calls && !identity_calls && !bindings);
      }

      if (phase == 1) {
         assert(identity_calls && !bindings);
      }
      assert(rr_rtp_socket_active(owner, now) == (phase == 2));
   }

   assert(bindings);
   rr_rtp_socket_close(client);
   rr_rtp_socket_close(server);
   puts("PASS: UDP source filters deny before network/identity binding and permit authenticated RTP probes");
}
