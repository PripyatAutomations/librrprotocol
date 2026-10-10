#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

time_t now;
bool dying, restarting;
static unsigned hello_events, sends, tls_calls;
extern void http_handler(struct mg_connection *, int, void *);
extern const char *tls_ca_path;
extern struct mg_str tls_ca_path_str;
extern const char *server_name;
const char *get_server_property(const char *server, const char *name) {
   return !strcmp(name, "server.url") ? "wss://example.test/ws" : NULL;
}
void mg_tls_init(struct mg_connection *c, const struct mg_tls_opts *opts) {
   assert(c->is_tls && !mg_strcmp(opts->name, mg_str("example.test")));
   tls_calls++;
}
static struct mg_connection *last_dest;
static char last_text[4096];
void event_emit_dict(const char *event, rrconn_t *client, dict *d) {
   (void)client;
   (void)d;

   if (!strcmp(event, "connected")) {
      assert(tls_calls == 1);
   }

   if (!strcmp(event, "hello")) {
      hello_events++;
   }
}
size_t mg_ws_send(struct mg_connection *c, const void *buf, size_t len, int opcode) {
   if (opcode == WEBSOCKET_OP_TEXT && buf) {
      assert(len < sizeof(last_text));
      memcpy(last_text, buf, len);
      last_text[len] = '\0';
   }
   sends++;
   last_dest = c;

   return len;
}

int main(void) {
   assert(parse_freq("7200") == 7200000);
   assert(parse_freq("7.2MHz") == 7200000);
   assert(parse_freq("nan") == -1 && parse_freq("1e999") == -1);
   assert(parse_freq(NULL) == -1 && parse_freq("7200junk") == -1);
   assert(parse_freq("9999999999999999999999MHz") == -1);
   struct mg_connection c = {
      0
   }, other_conn = {
      0
   };
   rrconn_t client = {
      .conn = &c
   }, other = {
      .conn = &other_conn
   };
   c.fn_data = &client;
   c.is_tls = true;
   server_name = "test";
   tls_ca_path = "*";
   tls_ca_path_str = mg_str("*");
   http_handler(&c, MG_EV_CONNECT, NULL);
   assert(tls_calls == 1);
   c.is_tls = false;
   char valid[] = "{\"msg\":{\"type\":\"hello\"},\"hello\":{\"swver\":\"test\"}}";
   struct mg_ws_message message = {
      .data = {
         .buf = valid, .len = strlen(valid)
      }, .flags = WEBSOCKET_OP_TEXT
   };
   assert(ws_handle(&client, &message));
   assert(hello_events == 1);
   message.flags = WEBSOCKET_OP_PING;
   assert(!ws_handle(&client, &message));
   assert(hello_events == 1);
   message.flags = WEBSOCKET_OP_TEXT;
   char hidden[256];
   strcpy(hidden, valid);
   size_t n = strlen(valid);
   memcpy(hidden + n + 1, "trailing", 8);
   message.data.buf = hidden;
   message.data.len = n + 9;
   assert(!ws_handle(&client, &message));
   assert(hello_events == 1);
   char array[] = "[{\"msg\":{\"type\":\"hello\"}}]";
   message.data.buf = array;
   message.data.len = strlen(array);
   assert(!ws_handle(&client, &message));
   assert(hello_events == 1);
   // Oversized text is refused before reading a short backing buffer.
   message.data.buf = valid;
   message.data.len = HTTP_WS_MAX_MSG + 1;
   assert(!ws_handle(&client, &message));
   uint8_t *packet = NULL;
   const char payload[] = "test";
   int length = rr_binframe_frame(&packet, RR_BINFRAME_SUBSYS_AUDIO, "pc16", RR_BINFRAME_DIR_RX, 0, 0, 1, 1, 0, payload, sizeof(payload));
   assert(length > 0);
   struct rr_binframe f;
   assert(!rr_binframe_parse(packet, length, &f));
   assert(rr_binframe_parse(packet, length - 1, &f) < 0);
   assert(rr_binframe_parse(packet, length + 1, &f) < 0);
   free(packet);
   client.is_ws = other.is_ws = true;
   client.authenticated = other.authenticated = true;
   strcpy(client.chatname, "TARGET");
   strcpy(other.chatname, "OTHER");
   client.next = &other;
   http_client_list = &client;
   struct mg_str data = {
      .buf = "private", .len = 7
   };
   sends = 0;
   ws_send_to_name(NULL, "target", &data, WEBSOCKET_OP_TEXT);
   assert(sends == 1 && last_dest == &c);
   client.authenticated = false;
   sends = 0;
   ws_send_to_name(&other, "TARGET", &data, WEBSOCKET_OP_TEXT);
   assert(!sends);
   /* Semantic dictionary sends use the same frame adapter, and dictionaries
    * can never masquerade as binary media. */
   dict *notice = dict_new();
   dict_add(notice, "msg.type", "notice");
   dict_add(notice, "notice.msg", "structured text");
   sends = 0;
   assert(!ws_send_dict(NULL, &client, notice, WEBSOCKET_OP_BINARY) && !sends);
   assert(ws_send_dict(NULL, &client, notice, WEBSOCKET_OP_TEXT));
   assert(sends == 1 && last_dest == &c);
   dict *sent = json2dict(last_text);
   assert(sent && !strcmp(dict_get(sent, "notice.msg", ""), "structured text"));
   dict_free(sent);
   dict_free(notice);
   sends = 0;
   assert(ws_kick_client_by_c(&c, "test kick"));
   assert(sends == 2 && c.is_draining);
   sent = json2dict(last_text);
   assert(sent && !strcmp(dict_get(sent, "msg.type", ""), "auth") &&
      strstr(dict_get(sent, "auth.error", ""), "test kick"));
   dict_free(sent);
   http_client_list = NULL;
   free(client.cli_version);
   puts("PASS: WebSocket opcodes/NUL/root/size, exact binary lengths and named-session delivery");

   return 0;
}
