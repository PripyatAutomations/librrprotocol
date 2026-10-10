#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <librrprotocol/server.url.h>

int main(void) {
   rr_server_url_t url;
   assert(rr_server_url_parse("ws://localhost:8420/ws/", &url));
   assert(url.transport == RR_TRANSPORT_WS && !url.tls && !url.irc);
   assert(url.port == 8420 && !strcmp(url.path, "/ws/"));
   assert(rr_server_url_parse("wss://example.test:443/ws/?token=x", &url));
   assert(url.transport == RR_TRANSPORT_WSS && url.tls && !url.irc);
   assert(rr_server_url_parse("irc://chat.example.test:6667", &url));
   assert(url.transport == RR_TRANSPORT_IRC && !url.tls && url.irc);
   assert(rr_server_url_parse("ircs://[::1]:6697/", &url));
   assert(url.transport == RR_TRANSPORT_IRCS && url.tls && url.irc && url.ipv6);
   assert(!strcmp(url.host, "::1") && url.port == 6697);
   assert(rr_server_url_parse("WSS://example.test:443", &url) && url.tls);
   const char *invalid[] = {
      "localhost:8420", "http://example.test:80", "ws://localhost/ws/",
      "irc://localhost", "ircs://localhost:0", "ws://localhost:65536/ws/",
      "ws://localhost:-1/ws/", "ws://localhost:8420x/ws/", "ws://:8420/ws/",
      "ws://user:pass@localhost:8420/ws/", "irc://localhost:6667/channel",
      "ws://localhost:8420/ws/#fragment", "ws://localhost:8420/\r\nQUIT",
      "irc://::1:6667", "irc://[not-ipv6]:6667", "irc://[::1]",
      "ws://localhost:99999999999999999999/ws/", "ws://local host:80/",
      "ws://localhost:80\\other", "ws://localhost:80?query", ""
   };
   for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
      assert(!rr_server_url_parse(invalid[i], &url));
   }
   assert(!rr_server_url_parse(NULL, &url));
   assert(!rr_server_url_parse("ws://localhost:80", NULL));
   puts("PASS: explicit URL schemes/ports, TLS selection, IPv6 and malformed addresses");
   return 0;
}
