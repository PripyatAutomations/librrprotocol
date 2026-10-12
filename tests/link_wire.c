#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librrprotocol/link.wire.h>
int main(void) {
   const char *hello =
      "{\"op\":\"link.hello\",\"node\":\"uuid\",\"site\":\"site\",\"root\":\"\",\"username\":\"peer\",\"password\":\"private\",\"role\":\"child\"}";
   dict *d = rr_link_wire_decode(hello, strlen(hello));
   assert(d);
   char *encoded = rr_link_wire_encode(d);
   assert(encoded);
   dict *copy = rr_link_wire_decode(encoded, strlen(encoded));
   assert(copy && !strcmp(dict_get(copy, "password", ""), "private"));
   free(encoded);
   dict_free(copy);
   dict_free(d);
   dict *app = dict_new();
   dict_add(app, "op", "link.application");
   dict_add(app, "node", "owner");
   dict_add(app, "dest", "destination");
   dict_add(app, "kind", "request");
   dict_add(app, "session", "session");
   dict_add(app, "username", "CALLSIGN");
   dict_add(app, "payload", "{\"msg\":{\"type\":\"cat\"}}");
   encoded = rr_link_wire_encode(app);
   assert(encoded);
   copy = rr_link_wire_decode(encoded, strlen(encoded));
   assert(copy);
   assert(!strcmp(dict_get(copy, "payload", ""), dict_get(app, "payload", "")));
   free(encoded);
   dict_free(copy);
   dict_add(app, "payload", "literal \\u0000 remains text");
   encoded = rr_link_wire_encode(app);
   assert(encoded);
   copy = rr_link_wire_decode(encoded, strlen(encoded));
   assert(copy);
   free(encoded);
   dict_free(copy);
   dict_add(app, "kind", "media");
   assert(!rr_link_wire_encode(app));
   dict_add(app, "kind", "request");
   char oversized[24002];
   memset(oversized, 'a', sizeof(oversized) - 1);
   oversized[sizeof(oversized) - 1] = 0;
   dict_add(app, "payload", oversized);
   assert(!rr_link_wire_encode(app));
   dict_free(app);
   const char *invalid[] = {
      "{}", "[]", "{\"op\":\"link.ready\",\"op\":\"link.ready\"}",
      "{\"op\":\"link.ready\",\"admin\":true}",
      "{\"op\":\"link.snapshot\",\"nodes\":[]}",
      "{\"op\":\"link.ready\"} garbage",
      "{\"op\":\"link.ready\"}{\"op\":\"link.ready\"}",
      "{\"op\":\"link.withdraw\",\"node\":\"a\\u0000b\"}",
      "{\"op\":\"link.hello\",\"node\":\"uuid\"}",
      "{\"op\":\"talk.msg\"}"
   };

   for (size_t i = 0 ; i < sizeof(invalid) / sizeof(invalid[0]) ; i++) {
      assert(!rr_link_wire_decode(invalid[i], strlen(invalid[i])));
   }

   const char nul[] = "{\"op\":\"link.ready\"}\0extra";
   assert(!rr_link_wire_decode(nul, sizeof(nul) - 1));
   char *huge = calloc(1, RR_LINK_MAX_FRAME + 2);
   assert(huge);
   memset(huge, ' ', RR_LINK_MAX_FRAME + 1);
   assert(!rr_link_wire_decode(huge, RR_LINK_MAX_FRAME + 1));
   free(huge);
   puts("PASS: separate link wire operations, bounded fields, duplicate/unknown/type/NUL rejection");
}
