//
// ws.bcast.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

extern time_t now;

#ifdef	USE_MONGOOSE
// Broadcast a message to all WebSocket clients (using http_client_list)
void ws_broadcast(rrconn_t *sender, struct mg_str *msg_data, int data_type) {
   if (!msg_data) {
      return;
   }
   rrconn_t *current = http_client_list;
   while (current) {
      // NULL sender means it came from the server itself
      if ( (current->is_ws && current->authenticated) && (current != sender) ) {
         mg_ws_send(current->conn, msg_data->buf, msg_data->len, data_type);
      }
      current = current->next;
   }
}

// Broadcast a message to all WebSocket clients with matching flags (using
// http_client_list)
void ws_broadcast_with_flags(u_int32_t flags, rrconn_t *sender, struct mg_str *msg_data, int data_type) {
   if (!msg_data) {
      return;
   }
   rrconn_t *current = http_client_list;
   while (current) {
      // NULL sender means it came from the server itself
      if (current && (current->is_ws && current->authenticated) && (current != sender) ) {
         if (client_has_flag(current, flags) ) {
            mg_ws_send(current->conn, msg_data->buf, msg_data->len, data_type);
         }
      }
      current = current->next;
   }
}

void ws_broadcast_audio(rrconn_t *sender, struct mg_str *msg_data, int data_type, u_int32_t channel) {
   if (!msg_data) {
      return;
   }
   rrconn_t *current = http_client_list;
   while (current) {
      // NULL sender means it came from the server itself
      if ( (current->is_ws && current->authenticated) && (current != sender) ) {
         // XXX: Compare the connection's codec
//         if (current->rx_codecs[
//         mg_ws_send(current->conn, msg_data->buf, msg_data->len, data_type);
      }
      current = current->next;
   }
}
#endif // defined(USE_MONGOOSE)

bool send_global_alert(const char *sender, const char *data) {
   if (!data) {
      return true;
   }
   const char *escaped_msg = escape_html(data);

   dict *alert_msg = dict_new();
   dict_add(alert_msg, "alert.from", sender);
   dict_add(alert_msg, "alert.msg", escaped_msg);
   dict_add_ulong(alert_msg, "alert.ts", now);

   ws_broadcast_dict(NULL, alert_msg, WEBSOCKET_OP_TEXT);
   free( (char *)escaped_msg );
   dict_free(alert_msg);

   return false;
}

bool ws_send_dict(rrconn_t *sender, rrconn_t *dest, dict *d, int data_type) {
   (void)sender;

   if (!d || !dest || !dest->conn) {
      Log(LOG_WARN, "rrproto.srv",
         "Unable to send msg dict:<%p> to conn:<%p> - invalid destination",
         d, dest);
      return false;
   }

   const char *jp = dict2json(d);

   if (!jp) {
      Log(LOG_WARN, "rrproto.srv",
         "Unable to serialize msg dict:<%p> to conn:<%p>",
         d, dest);
      return false;
   }

   Log(LOG_CRAZY, "ws.proto",
      "Sending dict <%p> to conn <%p>: %s",
      (void *)d, (void *)dest, jp);

   mg_ws_send(dest->conn, jp, strlen(jp), data_type);

   free((void *)jp);

   return true;
}

// Broadcast a message to all WebSocket clients (using http_client_list)
void ws_broadcast_dict(rrconn_t *sender, dict *d, int data_type) {
   if (!d) {
      return;
   }
   rrconn_t *current = http_client_list;
   while (current) {
      // NULL sender means it came from the server itself
      if ( (current->is_ws && current->authenticated) && (current != sender) ) {
         ws_send_dict(NULL, current, d, data_type);
      }
      current = current->next;
   }
}

// Broadcast a message to all WebSocket clients with matching flags (using http_client_list)
void ws_broadcast_dict_with_flags(u_int32_t flags, rrconn_t *sender, dict *d, int data_type) {
   if (!d) {
      return;
   }
   rrconn_t *current = http_client_list;
   while (current) {
      // NULL sender means it came from the server itself
      if (current && (current->is_ws && current->authenticated) && (current != sender) ) {
         if (client_has_flag(current, flags) ) {
            ws_send_dict(NULL, current, d, data_type);
         }
      }
      current = current->next;
   }
}
