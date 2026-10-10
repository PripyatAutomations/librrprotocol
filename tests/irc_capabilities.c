#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/irc.h>
time_t now = 100;
bool dying, restarting;
static unsigned changes;
static void changed(const char *event, const char *data, rrconn_t *c, void *user) {
   (void)event;
   (void)data;
   (void)c;
   (void)user;
   changes++;
}
static void wire(struct mg_connection *c, const char *expected) {
   assert(c->send.len == strlen(expected));
   assert(!memcmp(c->send.buf, expected, c->send.len));
   mg_iobuf_del(&c->send, 0, c->send.len);
}
int main(void) {
   rrconn_t a = {
      .fd = -1, .connected = now
   }, b = {
      .fd = -1
   };
   struct mg_connection c = {
      0
   };
   a.conn = &c;
   event_init();
   assert(!irc_init());
   event_on("irc.capabilities", changed, NULL);
   irc_capabilities_reset(&a);
   assert(!irc_supports_rustyrig(&a));
   irc_process_message(&a, ":server CAP * LS * :multi-prefix RUSTYRIG-other");
   assert(!c.send.len && !irc_supports_rustyrig(&a));
   irc_process_message(&a, ":server CAP * LS :RUSTYRIG sasl");
   wire(&c, "CAP REQ :multi-prefix RUSTYRIG\r\n");
   assert(!irc_supports_rustyrig(&a));
   irc_process_message(&a, ":server CAP * ACK :multi-prefix RUSTYRIG");
   wire(&c, "CAP END\r\n");
   assert(irc_supports_rustyrig(&a) && !irc_supports_rustyrig(&b) && changes == 1);
   irc_process_message(&a, ":server CAP * DEL :RUSTYRIG");
   assert(!irc_supports_rustyrig(&a) && changes == 2);
   irc_process_message(&a, ":server 005 nick RUSTYRIG PREFIX=(ov)@+ CHANMODES=beI,k,l,imnpst :supported");
   assert(irc_supports_rustyrig(&a));
   assert(irc_prefix_mode(&a, '@') == 'o' && irc_prefix_mode(&a, '+') == 'v' && !irc_prefix_mode(&a, '~'));
   assert(!strcmp(irc_modes_symbol(&a, "vo"), "@"));
   assert(!strcmp(irc_modes_symbol(&a, "v"), "+"));
   assert(irc_mode_has_argument(&a, 'b', false) && irc_mode_has_argument(&a, 'k', false));
   assert(irc_mode_has_argument(&a, 'l', true) && !irc_mode_has_argument(&a, 'l', false));
   assert(!irc_mode_has_argument(&a, 'i', true));
   irc_process_message(&a, ":server 005 nick -RUSTYRIG PREFIX=(ov)!? :supported");
   assert(!irc_supports_rustyrig(&a) && !strcmp(irc_modes_symbol(&a, "o"), "!"));
   irc_capabilities_reset(&a);
   irc_process_message(&a, ":server CAP * LS :sasl echo-message");
   wire(&c, "CAP END\r\n");
   irc_capabilities_reset(&a);
   irc_process_message(&a, ":server CAP * LS :RUSTYRIG");
   wire(&c, "CAP REQ :RUSTYRIG\r\n");
   irc_process_message(&a, ":server CAP * NAK :RUSTYRIG");
   wire(&c, "CAP END\r\n");
   assert(!irc_supports_rustyrig(&a));
   irc_capabilities_clear(&a);
   irc_shutdown();
   event_shutdown();
   mg_iobuf_free(&c.send);
   puts("PASS: isolated IRC capabilities, multiline CAP negotiation, rejection, removal, PREFIX and mode arguments");
}
