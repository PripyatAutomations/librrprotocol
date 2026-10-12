// Application-owned routing hooks; libraries retain validation and dispatch.
#ifndef RR_SERVER_EVENTS_H
#define RR_SERVER_EVENTS_H
#include <librustyaxe/core.h>
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
bool ws_process_dict(rrconn_t *client, dict *message);
#endif
