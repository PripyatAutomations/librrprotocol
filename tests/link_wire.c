#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librrprotocol/link.wire.h>
int main(void) {
   const char *hello = "{\"op\":\"link.hello\",\"node\":\"uuid\",\"site\":\"site\",\"password\":\"private\"}";
   dict *d = rr_link_wire_decode(hello, strlen(hello));
   assert(d && !dict_get(d, "username", NULL));
   char *encoded = rr_link_wire_encode(d);
   assert(encoded);
   dict *copy = rr_link_wire_decode(encoded, strlen(encoded));
   assert(copy && !strcmp(dict_get(copy, "password", ""), "private"));
   free(encoded);
   dict_free(copy);
   dict_free(d);
   const char *timed_hello = "{\"op\":\"link.hello\",\"node\":\"uuid\",\"site\":\"site\",\"password\":\"private\",\"clock.unix\":\"1792624453\"}";
   d = rr_link_wire_decode(timed_hello, strlen(timed_hello));
   assert(d && !strcmp(dict_get(d, "clock.unix", ""), "1792624453"));
   encoded = rr_link_wire_encode(d);
   assert(encoded);
   free(encoded);
   dict_free(d);
   dict *caps = dict_new();
   dict_add(caps, "op", "link.capabilities");
   dict_add(caps, "version", "2.1");
   dict_add(caps, "features", "rig.menu,object.model");
   encoded = rr_link_wire_encode(caps);
   assert(encoded);
   copy = rr_link_wire_decode(encoded, strlen(encoded));
   assert(copy && !strcmp(dict_get(copy, "version", ""), "2.1"));
   free(encoded);
   dict_free(copy);
   dict_free(caps);
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
   /* Sync control messages must round-trip through the strict wire codec. */
   const char *sync_kinds[] = {
      "catalog-begin", "catalog", "catalog-end", "catalog-ack"
   };

   for (size_t i = 0 ; i < sizeof(sync_kinds) / sizeof(sync_kinds[0]) ; i++) {
      dict_add(app, "kind", sync_kinds[i]);
      dict_add(app, "session", "0c930d3c-3f88-4cf9-bc6c-a89ccc25f762");
      dict_add(app, "username", "");
      dict_add(app, "payload", "{}");
      encoded = rr_link_wire_encode(app);
      assert(encoded);
      copy = rr_link_wire_decode(encoded, strlen(encoded));
      assert(copy && !strcmp(dict_get(copy, "kind", ""), sync_kinds[i]));
      free(encoded);
      dict_free(copy);
   }

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
      "{\"op\":\"link.hello\",\"node\":\"uuid\",\"site\":\"site\",\"username\":\"peer\",\"password\":\"private\"}",
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
