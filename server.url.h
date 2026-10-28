//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
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
} rr_url_transport_t;
typedef struct {
   rr_url_transport_t transport;
   char host[HOSTLEN + 1];
   bool credentials;
   char user[NICKLEN + 1];
   char pass[PASSLEN + 1];
   uint16_t port;
   bool tls;
   bool irc;
   bool ipv6;
   const char *path;            /* Borrowed from the input URL. */
} rr_server_url_t;

/* Require a supported scheme, host; omitted ports use transport defaults. False means invalid. */
bool rr_server_url_parse(const char *url, rr_server_url_t *out);
#endif
