// librrprotocol/connman.c
// Centralized connection manager implementation for librrprotocol

#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librustyaxe/event-bus.h>
#include <librrprotocol/connman.h>

#if defined(USE_MONGOOSE)
extern struct mg_mgr mgr;  // provided by the application that uses mongoose
#endif

// Shared state
char active_server[SERVERLEN] = { 0 };
rr_connection_t *active_connections = NULL;
int ws_connected = 0;
int ws_tx_connected = 0;
bool server_ptt_state = false;

rr_connection_t *connection_find(const char *server) {
   if (!server) {
      Log(LOG_DEBUG, "connman", "connection_find without server");

      return NULL;
   }
   rr_connection_t *cptr = active_connections;

   while (cptr) {
      if (strcasecmp(cptr->name, server) == 0) {
         Log(LOG_CRAZY, "connman", "Found server |%s| at <%p>", server, cptr);

         return cptr;
      }
      cptr = cptr->next;
   }
   return NULL;
}

bool connection_create(const char *server) {
   if (!server) {
      Log(LOG_DEBUG, "connman", "connection_create without server");

      return true;
   }
   rr_connection_t *cptr = connection_find(server);

   if (cptr) {
      Log(LOG_WARN, "connman", "Connection for server |%s| already exists", server);

      return true;
   }
   // Create a basic connection object and add it to the list
   rr_connection_t *new_conn = calloc( 1, sizeof(rr_connection_t) );

   if (!new_conn) {
      return true;
   }
   snprintf(new_conn->name, sizeof(new_conn->name), "%s", server);
   new_conn->connected = false;
   new_conn->ptt_active = false;
   new_conn->conn_type = NULL;
   new_conn->next = active_connections;
   active_connections = new_conn;

   return false;
}

bool connection_remove(rr_connection_t *conn) {
   if (!conn) {
      Log(LOG_DEBUG, "connman", "connection_remove called with no connection ptr");

      return true;
   }
   rr_connection_t **this_conn = &active_connections;
   while (*this_conn) {
      if (*this_conn == conn) {
         *this_conn = conn->next;
         free(conn);

         return false;
      }
      this_conn = &(*this_conn)->next;
   }
   return true;
}

const char *get_server_property(const char *server, const char *prop) {
   if (!server || !prop) {
      Log(LOG_CRIT, "ws", "get_server_prop with null server or prop");
      return NULL;
   }
   char fullkey[KEYLEN];
   memset( fullkey, 0, sizeof(fullkey) );
   snprintf(fullkey, sizeof(fullkey), "server:%s.%s", server, prop);

   return dict_get(cfg, fullkey, NULL);
}

// Public API used by callers. These call through to the underlying transport.
// NB: connection setup/teardown is client behavior and lives in rrclient/
// (connman.c). This library must not own or touch client connection state.
