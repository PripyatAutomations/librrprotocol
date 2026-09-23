#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "../srv.chat.c"

static const char *configured_station_name = "rplywv00";

const char *cfg_get_exp(const char *key) {
   if (strcmp(key, "station.name") != 0) return NULL;
   return strdup(configured_station_name);
}

int cfg_get_int(const char *key, int def) {
   (void)key;
   return def;
}

int main(void) {
   assert(strcmp(ws_authoritative_room(), "#rplywv00-rig0") == 0);
   assert(!ws_room_has_vfos("&localrig"));
   assert(ws_room_vfo_mask("#rplywv00-rig0") == 3);

   configured_station_name = "w8abc";
   assert(strcmp(ws_authoritative_room(), "#w8abc-rig0") == 0);
   assert(ws_room_has_vfos("#w8abc-rig0"));
   puts("PASS: station.name selects the authoritative rig0 room");
   return 0;
}
