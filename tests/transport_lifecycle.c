/* Asynchronous driver lifetime and capability/close boundaries. */
#include <librrprotocol/transport.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
struct fixture {
   unsigned closes, messages, events;
};
static int send_message(void *arg, const uint8_t *data, size_t len) {
   struct fixture *f = arg;
   assert(len == 4 && !memcmp(data, "test", 4));
   ++f->messages;

   return RR_TRANSPORT_OK;
}
static void close_driver(void *arg) {
   ++((struct fixture *)arg)->closes;
}
static bool pin(void *arg, uint8_t out[32]) {
   (void)arg;
   memset(out, 42, 32);

   return true;
}
static void event(rr_transport_t *t, enum rr_transport_event e, rr_transport_channel_t *ch, const uint8_t *data, size_t len, void *arg) {
   (void)t;
   (void)e;
   (void)ch;
   (void)data;
   (void)len;
   ++((struct fixture *)arg)->events;
}
int main(void) {
   struct fixture f = {
      0
   };
   rr_transport_driver_t driver = {
      .abi_version = RR_TRANSPORT_ABI_VERSION,
      .capabilities = RR_TRANSPORT_RELIABLE_MESSAGES | RR_TRANSPORT_TLS_AUTHENTICATED | RR_TRANSPORT_PEER_SPKI,
      .send_message = send_message, .peer_spki_sha256 = pin, .close = close_driver
   };
   rr_transport_t *t = rr_transport_new(&driver, &f, event, &f);
   uint8_t hash[32];
   assert(!rr_transport_peer_spki_sha256(t, hash));
   assert(rr_transport_send_message(t, "test", 4) == RR_TRANSPORT_CLOSED_ERROR);
   rr_transport_emit(t, RR_TRANSPORT_EVENT_OPEN, NULL, NULL, 0);
   assert(f.events == 1 && rr_transport_peer_spki_sha256(t, hash) && hash[0] == 42);
   assert(rr_transport_send_message(t, "test", 4) == RR_TRANSPORT_OK && f.messages == 1);
   assert(rr_transport_send_datagram(t, "test", 4) == RR_TRANSPORT_UNSUPPORTED);
   rr_transport_close(t);
   rr_transport_close(t);
   assert(f.closes == 1 && rr_transport_state(t) == RR_TRANSPORT_CLOSING);
   rr_transport_free(t);        /* Must retain the handle for deferred driver CLOSE. */
   assert(f.closes == 1);
   rr_transport_emit(t, RR_TRANSPORT_EVENT_OPEN, NULL, NULL, 0);
   rr_transport_emit(t, RR_TRANSPORT_EVENT_MESSAGE, NULL, "test", 4);
   assert(f.events == 1 && !rr_transport_peer_spki_sha256(t, hash));
   rr_transport_emit(t, RR_TRANSPORT_EVENT_CLOSE, NULL, NULL, 0);
   rr_transport_emit(t, RR_TRANSPORT_EVENT_ERROR, NULL, NULL, 0);
   assert(f.events == 2 && rr_transport_state(t) == RR_TRANSPORT_CLOSED);
   rr_transport_free(t);
   puts("PASS: transport capabilities and deferred close lifetime");

   return 0;
}
