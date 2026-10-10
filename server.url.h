#ifndef RR_SERVER_URL_H
#define RR_SERVER_URL_H
#include <stdbool.h>
#include <stdint.h>
#include <librustyaxe/struct.h>

typedef enum {
   RR_TRANSPORT_WS,
   RR_TRANSPORT_WSS,
   RR_TRANSPORT_IRC,
   RR_TRANSPORT_IRCS
} rr_transport_t;
typedef struct {
   rr_transport_t transport;
   char host[HOSTLEN + 1];
   uint16_t port;
   bool tls;
   bool irc;
   bool ipv6;
   const char *path; /* Borrowed from the input URL. */
} rr_server_url_t;

/* Require a supported scheme, host and explicit port. False means invalid. */
bool rr_server_url_parse(const char *url, rr_server_url_t *out);
#endif
