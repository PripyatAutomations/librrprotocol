// Application-owned routing hooks; libraries retain validation and dispatch.
#ifndef RR_SERVER_EVENTS_H
#define RR_SERVER_EVENTS_H
#include <librustyaxe/core.h>
#define RR_SERVER_HTTP_EVENT "protocol.server.http"
#define RR_SERVER_ROUTE_EVENT "protocol.server.route"
#define RR_SERVER_SEND_EVENT "protocol.server.send"
#define RR_SERVER_PUBLISH_EVENT "protocol.server.publish"
#define RR_SERVER_ROSTER_EVENT "protocol.server.roster"
typedef struct {
   dict *message;
   bool handled, result;
} rr_server_io_t;
typedef struct {
   dict *message;
   const char *room;
} rr_server_publish_t;
struct mg_connection;
struct mg_http_message;
typedef struct {
   struct mg_connection *connection;
   struct mg_http_message *request;
   bool handled, detach_client;
} rr_server_http_t;
#define RR_MEDIA_REQUEST_EVENT "protocol.media.request"
#define RR_MEDIA_BINARY_EVENT "protocol.media.send"
#define RR_MEDIA_INBOUND_EVENT "protocol.media.receive"
#define RR_MEDIA_FANOUT_EVENT "protocol.media.fanout"
#define RR_MEDIA_DEMAND_EVENT "protocol.media.demand"
struct rr_mediachan;
typedef struct {
   const uint8_t *frame;
   size_t length;
   bool setup, handled, result;
} rr_media_binary_t;
typedef struct {
   struct rr_mediachan *channel;
   const uint8_t *frame;
   size_t length;
   rrconn_t *target, *exclude;
   bool setup;
} rr_media_fanout_t;
typedef struct { const struct rr_mediachan *channel; bool interested; } rr_media_demand_t;
bool ws_binframe_process_client(rrconn_t *client, const char *data, size_t length);
bool ws_process_dict(rrconn_t *client, dict *message);
#endif
