// librrprotocol/server.url.c: Dispatcher for server url types
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Add new protocol types here to have them supported by server blocks, /server, etc
//
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <librrprotocol/server.url.h>

/* Decode URL userinfo without treating '+' as a form-encoded space. */
static bool decode_userinfo(const char *begin, const char *end, char *out, size_t capacity) {
   size_t used = 0;
   while (begin < end) {
      unsigned char value = (unsigned char)*begin++;

      if (value == '%') {
         if (end - begin < 2 || !isxdigit((unsigned char)begin[0]) || !isxdigit((unsigned char)begin[1])) {
            return false;
         }
         value = 0;

         for (int i = 0 ; i < 2 ; i++) {
            unsigned char digit = (unsigned char)*begin++;
            value = value * 16 + (isdigit(digit) ? digit - '0' : tolower(digit) - 'a' + 10);
         }
      }

      if (value < ' ' || value == 127 || used + 1 >= capacity) {
         return false;
      }
      out[used++] = value;
   }
   out[used] = '\0';

   return true;
}

bool rr_server_url_parse(const char *url, rr_server_url_t *out) {
   if (!url || !out || !*url) {
      return false;
   }
   rr_server_url_t parsed = {
      0
   };
   const char *host;

   if (!strncasecmp(url, "ws://", 5)) {
      parsed.transport = RR_TRANSPORT_WS;
      parsed.port = 8420;
      host = url + 5;
   } else if (!strncasecmp(url, "wss://", 6)) {
      parsed.transport = RR_TRANSPORT_WSS;
      parsed.port = 4420;
      parsed.tls = true;
      host = url + 6;
   } else if (!strncasecmp(url, "irc://", 6)) {
      parsed.transport = RR_TRANSPORT_IRC;
      parsed.port = 6667;
      parsed.irc = true;
      host = url + 6;
   } else if (!strncasecmp(url, "ircs://", 7)) {
      parsed.transport = RR_TRANSPORT_IRCS;
      parsed.port = 6697;
      parsed.irc = parsed.tls = true;
      host = url + 7;
   } else {
      return false;
   }

   for (const unsigned char *p = (const unsigned char *)url ; *p ; p++) {
      if (*p <= ' ' || *p == 127 || *p == '#' || *p == '\\') {
         return false;
      }
   }

   const char *authority_end = host + strcspn(host, "/");
   const char *at = memchr(host, '@', (size_t)(authority_end - host));

   if (at) {
      const char *colon = memchr(host, ':', (size_t)(at - host));

      if (parsed.irc || !colon || colon == host ||
         !decode_userinfo(host, colon, parsed.user, sizeof(parsed.user)) || !decode_userinfo(colon + 1, at, parsed.pass, sizeof(parsed.pass))) {
         return false;
      }
      parsed.credentials = true;
      host = at + 1;
   }

   const char *end;

   if (*host == '[') {
      parsed.ipv6 = true;
      host++;
      end = strchr(host, ']');

      if (!end || (end[1] && end[1] != ':' && end[1] != '/')) {
         return false;
      }
   } else {
      end = host + strcspn(host, " :/");

      for (const unsigned char *p = (const unsigned char *)host ; p < (const unsigned char *)end ; p++) {
         if (!isalnum(*p) && *p != '-' && *p != '.') {
            return false;
         }
      }
   }
   size_t length = (size_t)(end - host);

   if (!length || length >= sizeof(parsed.host)) {
      return false;
   }
   memcpy(parsed.host, host, length);
   parsed.host[length] = '\0';

   if (parsed.ipv6) {
      struct in6_addr address;

      if (inet_pton(AF_INET6, parsed.host, &address) != 1) {
         return false;
      }
   }
   const char *port = end + (parsed.ipv6 ? 1 : 0);

   if (*port != ':') {
      if ((*port && *port != '/') || (parsed.irc && *port && strcmp(port, "/"))) {
         return false;
      }
      parsed.path = *port ? port : (parsed.irc ? "" : "/");
      *out = parsed;

      return true;
   }
   port++;
   unsigned value = 0;

   if (!isdigit((unsigned char)*port)) {
      return false;
   }
   while (isdigit((unsigned char)*port)) {
      value = value * 10 + (unsigned)(*port++ - '0');

      if (value > 65535) {
         return false;
      }
   }

   if (!value || (*port && *port != '/') || (parsed.irc && *port && strcmp(port, "/"))) {
      return false;
   }
   parsed.port = (uint16_t) value;
   parsed.path = *port ? port : (parsed.irc ? "" : "/");
   *out = parsed;

   return true;
}
