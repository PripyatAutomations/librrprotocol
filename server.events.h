//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Application-owned routing hooks; libraries retain validation and dispatch.
#ifndef RR_SERVER_EVENTS_H
#define RR_SERVER_EVENTS_H
#include <librustyaxe/core.h>
#define RR_SERVER_ADMISSION_EVENT "protocol.server.admission"
typedef struct {
   const char *listener, *ip;
   const http_user_t *account;  /* NULL until credentials have been verified. */
   bool allowed;
} rr_server_admission_t;
#define RR_SERVER_HTTP_EVENT "protocol.server.http"
#define RR_SERVER_SUPERVISION_EVENT "protocol.server.supervision"
typedef struct {
   const char *room;
   bool request, allowed;
} rr_server_supervision_t;
bool ws_supervision_allowed(rrconn_t *client, const char *room, bool request);
#define RR_SERVER_ROUTE_EVENT "protocol.server.route"
#define RR_SERVER_SEND_EVENT "protocol.server.send"
#define RR_SERVER_PUBLISH_EVENT "protocol.server.publish"
#define RR_SERVER_TALKER_EVENT "protocol.server.talker"
typedef struct {
   rrconn_t *client;
} rr_server_talker_t;
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
#define RR_MEDIA_CATALOG_EVENT "protocol.media.catalog"
/* Synchronous application policy, evaluated for subscribe, codec and fanout. Listeners may deny; clients/protocol library do not own bridge permissions. */
#define RR_MEDIA_ACCESS_EVENT "protocol.media.access"
typedef struct {
   const struct rr_mediachan *channel;
   bool allowed;
} rr_media_access_t;
#define RR_MEDIA_DEMAND_EVENT "protocol.media.demand"
struct rr_mediachan;
typedef struct {
   struct rr_mediachan *channel;
   bool removed;
} rr_media_catalog_t;
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
typedef struct {
   const struct rr_mediachan *channel;
   bool interested;
} rr_media_demand_t;
bool ws_binframe_process_client(rrconn_t *client, const char *data, size_t length);
bool ws_process_dict(rrconn_t *client, dict *message);
#endif
