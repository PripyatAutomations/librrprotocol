// PTT supervision uses current account roles and the actual holder's target.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
time_t now;
bool dying, restarting;
static unsigned requests;
static char error[1024];
static rrconn_t *subject;
bool ws_send_dict(rrconn_t *from, rrconn_t *to, dict *d, int type) {
   const char *message = dict_get(d, "error.msg", NULL);
   if (message) snprintf(error, sizeof(error), "%s", message);
   return true;
}
void event_emit_dict(const char *event, rrconn_t *client, dict *d) {
   if (!strcmp(event, "rigctl")) {
      requests++; subject = client;
      assert(!strcmp(dict_get(d, "rigctl.room", ""), "#authority-rig0"));
      assert(!strcmp(dict_get(d, "rigctl.vfo", ""), "B"));
   }
}
int main(void) {
   cfg = dict_new(); dict_add(cfg, "station.name", "authority");
   assert(ws_room_set_vfo_mask("#authority-rig0", 3));
   ws_set_authoritative_room("#authority-rig0");
   rrconn_t actor = { .authenticated = true, .is_ws = true, .user = &http_users[1] };
   rrconn_t holder = { .authenticated = true, .is_ws = true, .user = &http_users[2] };
   actor.user->uid = 1; holder.user->uid = 2;
   actor.user->enabled = holder.user->enabled = true;
   strcpy(actor.chatname, "ACTOR"); strcpy(holder.chatname, "HOLDER");
   strcpy(actor.user->name, "ACTOR"); strcpy(holder.user->name, "HOLDER");
   holder.next = &actor; http_client_list = &holder;
   dict *d = dict_new(); dict_add(d, "cat.cmd", "ptt"); dict_add(d, "cat.vfo", "A");
   dict_add(d, "cat.room", "#unrelated"); dict_add_bool(d, "cat.ptt", false);
   const struct { const char *actor, *holder; bool allowed; } cases[] = {
      {"owner", "tx", true}, {"admin", "owner", false},
      {"owner", "admin", true}, {"owner", "owner", false}, {"admin", "admin", false},
      {"admin", "tx", true},
      {"tx", "noob", true}, {"elmer", "noob", true},
      {"tx", "tx", false}, {"elmer", "elmer", false},
      {"elmer", "tx", false}, {"tx", "admin", false},
      {"rx,chat", "noob", false}, {"noob", "noob", false}
   };
   for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      strcpy(actor.user->privs, cases[i].actor); strcpy(holder.user->privs, cases[i].holder);
      holder.is_ptt = true; holder.ptt_vfo = 'B'; strcpy(holder.ptt_room, "#authority-rig0");
      actor.is_ptt = false; requests = 0; subject = NULL;
      bool accepted = ws_handle_rigctl_msg(&actor, d);
      assert(accepted == cases[i].allowed);
      assert(holder.is_ptt != cases[i].allowed && !actor.is_ptt);
      assert(requests == (unsigned)cases[i].allowed);
      if (accepted) { assert(subject == &holder); }
   }
   // Key-down never takes over a held transmitter, regardless of role.
   strcpy(actor.user->privs, "tx"); strcpy(holder.user->privs, "noob");
   strcpy(actor.rooms, "#authority-rig0"); holder.is_ptt = true; holder.ptt_vfo = 'B';
   dict_add_bool(d, "cat.ptt", true); dict_add(d, "cat.room", "#authority-rig0");
   for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
      strcpy(actor.user->privs, cases[i].actor); strcpy(holder.user->privs, cases[i].holder);
      requests = 0;
      assert(!ws_handle_rigctl_msg(&actor, d));
      assert(holder.is_ptt && !actor.is_ptt && !requests);
   }
   holder.is_ptt = false; actor.is_ptt = false;
   strcpy(actor.user->privs, "tx");
   dict_add_bool(d, "cat.ptt", false);
   assert(!ws_handle_rigctl_msg(&actor, d));
   assert(strstr(error, "You do not hold PTT on VFO A in room #authority-rig0"));
   dict_add(d, "cat.cmd", "mode"); dict_add(d, "cat.mode", "BADMODE");
   assert(!ws_handle_rigctl_msg(&actor, d));
   assert(strstr(error, "BADMODE") && strstr(error, "VFO A") && strstr(error, "#authority-rig0"));
   dict_free(d); dict_free(cfg); cfg = NULL; http_client_list = NULL;
   puts("PASS: strict PTT stop hierarchy, TX/elmer supervision, actual holder targeting and no key-down takeover");
}
