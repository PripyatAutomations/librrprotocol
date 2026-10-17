#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "../srv.chat.c"

static const char *configured_station_name = "rplywv00";

const char *cfg_get_exp(const char *key) {
   if (strcmp(key, "station.name") != 0) {
      return NULL;
   }

   return strdup(configured_station_name);
}

int cfg_get_int(const char *key, int def) {
   (void)key;

   return def;
}

int main(void) {
   assert(!strcmp(ws_site_room(), "#rplywv00"));
   assert(ws_room_name_valid("#rig-room"));
   assert(ws_room_rig_base("#anything-rig123"));
   assert(!ws_room_rig_base("#anything-rig123.rx"));
   assert(ws_room_rig_namespace("#rplywv00-rig1"));
   assert(ws_room_rig_namespace("#rplywv00-rig1.rx"));
   assert(!ws_room_rig_namespace("#other-rig1.rx"));
   assert(!ws_room_rig_namespace("#rplywv00-rig1."));
   assert(!ws_room_rig_namespace("#rplywv00-rig1junk"));
   assert(!ws_room_set_vfo_mask("#chat", 3));
   assert(!ws_room_name_valid("#bad room"));
   assert(!ws_room_name_valid("#bad,room"));
   assert(ws_room_set_vfo_mask("#rplywv00-rig1.rx", 5));
   assert(ws_room_vfo_mask("#rplywv00-rig1.rx") == 5);
   ws_set_authoritative_vfo_mask(3);
   assert(strcmp(ws_authoritative_room(), "#rplywv00-rig0") == 0);
   assert(!ws_room_has_vfos("&localrig"));
   assert(ws_room_vfo_mask("#rplywv00-rig0") == 3);

   configured_station_name = "w8abc";
   assert(strcmp(ws_authoritative_room(), "#w8abc-rig0") == 0);
   assert(ws_room_has_vfos("#w8abc-rig0"));

   /* User-provided room overrides must remain channel names. */
   ws_set_authoritative_room("invalid");
   assert(strcmp(ws_authoritative_room(), "#w8abc-rig0") == 0);
   ws_set_authoritative_room("&");
   assert(strcmp(ws_authoritative_room(), "#w8abc-rig0") == 0);
   ws_set_authoritative_room("&localrig");
   assert(strcmp(ws_authoritative_room(), "&localrig") == 0);
   ws_set_authoritative_room("#custom-rig0");
   assert(strcmp(ws_authoritative_room(), "#custom-rig0") == 0);
   puts("PASS: station.name selects the authoritative rig0 room");

   return 0;
}
