// Regression test for releasing PTT when an authenticated client disconnects.
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/dict.h>
#include <librustyaxe/struct.h>
#include <librustyaxe/event-bus.h>
#include <librrprotocol/http.h>

/* Host globals referenced by the protocol library's shared object. */
time_t now;
bool dying;
bool restarting;

static int event_count;
static char event_name[32];
static char event_user[HTTP_USER_LEN + 1];
static char event_vfo;
static char event_room[128];
static bool event_ptt;

/* Capture the event that would be consumed by rrserver/events.c. */
void event_emit_dict(const char *event, rrconn_t *cptr, dict *data) {
   (void)cptr;
   snprintf(event_room, sizeof(event_room), "%s", dict_get(data, "cat.room", ""));
   event_count++;
   snprintf(event_name, sizeof(event_name), "%s", event ? event : "");
   snprintf(event_user, sizeof(event_user), "%s", dict_get(data, "cat.user", ""));
   const char *vfo = dict_get(data, "cat.vfo", "");
   event_vfo = (vfo && *vfo) ? vfo[0] : 0;
   event_ptt = dict_get_bool(data, "cat.ptt", true);
}

int main(void) {
   http_user_t admin_user = {
      0
   };
   snprintf(admin_user.name, sizeof(admin_user.name), "%s", "admin");
   snprintf(admin_user.privs, sizeof(admin_user.privs), "%s", "admin,radio,tx");
   admin_user.enabled = true;

   rrconn_t admin = {
      0
   };
   admin.authenticated = true;
   admin.user = &admin_user;
   admin.is_ptt = true;
   admin.ptt_vfo = 'A';
   snprintf(admin.ptt_room, sizeof(admin.ptt_room), "#other-rig1");
   snprintf(admin.chatname, sizeof(admin.chatname), "%s", "admin");

   ws_release_ptt_on_disconnect(&admin);
   assert(!admin.is_ptt);
   assert(event_count == 1);
   assert(strcmp(event_name, "rig.ptt") == 0);
   assert(strcmp(event_user, "admin") == 0);
   assert(event_vfo == 'A');
   assert(!strcmp(event_room, "#other-rig1"));
   assert(!event_ptt);

   /* A client that was receiving only must not generate a release event. */
   admin.ptt_vfo = 'B';
   ws_release_ptt_on_disconnect(&admin);
   assert(event_count == 1);

   puts("PASS: disconnecting admin with PTT on releases the keyed VFO");

   return 0;
}
