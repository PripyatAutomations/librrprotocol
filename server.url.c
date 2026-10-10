#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <librrprotocol/server.url.h>

bool rr_server_url_parse(const char *url, rr_server_url_t *out) {
   if (!url || !out || !*url) {
      return false;
   }
   rr_server_url_t parsed = {0};
   const char *host;
   if (!strncasecmp(url, "ws://", 5)) {
      parsed.transport = RR_TRANSPORT_WS;
      host = url + 5;
   } else if (!strncasecmp(url, "wss://", 6)) {
      parsed.transport = RR_TRANSPORT_WSS;
      parsed.tls = true;
      host = url + 6;
   } else if (!strncasecmp(url, "irc://", 6)) {
      parsed.transport = RR_TRANSPORT_IRC;
      parsed.irc = true;
      host = url + 6;
   } else if (!strncasecmp(url, "ircs://", 7)) {
      parsed.transport = RR_TRANSPORT_IRCS;
      parsed.irc = parsed.tls = true;
      host = url + 7;
   } else {
      return false;
   }
   for (const unsigned char *p = (const unsigned char *)url; *p; p++) {
      if (*p <= ' ' || *p == 127 || *p == '#' || *p == '\\') {
         return false;
      }
   }
   const char *end;
   if (*host == '[') {
      parsed.ipv6 = true;
      host++;
      end = strchr(host, ']');
      if (!end || end[1] != ':') {
         return false;
      }
   } else {
      end = strchr(host, ':');
      if (!end) {
         return false;
      }
      for (const unsigned char *p = (const unsigned char *)host;
           p < (const unsigned char *)end; p++) {
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
   const char *port = end + (parsed.ipv6 ? 2 : 1);
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
   if (!value || (*port && *port != '/') ||
       (parsed.irc && *port && strcmp(port, "/"))) {
      return false;
   }
   parsed.port = (uint16_t)value;
   parsed.path = *port ? port : (parsed.irc ? "" : "/");
   *out = parsed;
   return true;
}
