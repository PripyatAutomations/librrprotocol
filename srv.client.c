// http.c
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Here we deal with http requests using mongoose
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <arpa/inet.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>

extern time_t now;
#ifdef  USE_MONGOOSE
rrconn_t *http_find_client_by_c(struct mg_connection *c) {
   if (!c) {
      return NULL;
   }
   rrconn_t *cptr = http_client_list;
   int i = 0;

   while (cptr) {
      if (cptr->conn == c) {
         Log( LOG_CRAZY, "http.core", "find_client_by_c <%p> returning index %i: %p |%s|", c, i, cptr,
            (*cptr->chatname ? cptr->chatname : "<UNAUTHENTICATED>") );

         return cptr;
      }
      i++;
      cptr = cptr->next;
   }
   Log(LOG_CRAZY, "http.core", "find_client_by_c <%p> no matches!", c);

   return NULL;
}
#endif // defined(USE_MONGOOSE)

rrconn_t *http_find_client_by_token(const char *token) {
   if (!token) {
      return NULL;
   }
   rrconn_t *cptr = http_client_list;
   int i = 0;

   while (cptr) {
      if (cptr->token[0] == '\0') {
         continue;
      }

      if (memcmp( cptr->token, token, strlen(cptr->token) ) == 0) {
         Log( LOG_CRAZY, "http.core", "find_client_by_token |%s| returning index %i: %p |%s|", token, i, cptr,
            (*cptr->chatname ? cptr->chatname : "<UNAUTHENTICATED>") );

         return cptr;
      }
      i++;
      cptr = cptr->next;
   }
   Log(LOG_CRAZY, "http.core", "find client: no matches for token |%s|!", token);

   return NULL;
}

rrconn_t *http_find_client_by_guest_id(int gid) {
   rrconn_t *cptr = http_client_list;
   int i = 0;

   // this filters out invalid calls
   if (gid <= 1) {
      Log(LOG_WARN, "http", "find_client_by_guestid: gid %d isn't valid", gid);

      return NULL;
   }
   while (cptr) {
      if (cptr->guest_id == gid) {
         return cptr;
      }
      i++;
      cptr = cptr->next;
   }
   return NULL;
}

rrconn_t *http_find_client_by_name(const char *name) {
   rrconn_t *cptr = http_client_list;
   int i = 0;

   if (!name) {
      return NULL;
   }
   while (cptr) {
      Log(LOG_CRAZY, "http.core", "find client by name: i: %d user:<%p> chatname: %s", i, cptr->user, cptr->chatname);

      // incomplete entry
      if (!cptr->user || (cptr->chatname[0] == '\0') ) {
         cptr = cptr->next;
         continue;
      }

      // match?
      if (strcasecmp(cptr->chatname, name) == 0) {
         Log( LOG_CRAZY, "http.core", "find client by name |%s| found match at index %d: <%p> |%s|", name, i, cptr,
            (*cptr->chatname ? cptr->chatname : "<UNAUTHENTICATED>") );

         return cptr;
      }
      i++;
      cptr = cptr->next;
   }
   Log(LOG_DEBUG, "http.core", "find client by name found no results for %s, index was %d", name, i);

   return NULL;
}

void http_dump_clients(void) {
   rrconn_t *cptr = http_client_list;
   int i = 0;

   while (cptr) {
#if     defined(USE_MONGOOSE)
      Log(LOG_DEBUG, "http", " => %d at <%p> %sactive %swebsocket, conn: <%p>, next: <%p> ", i, cptr,
         (cptr->active ? "" : "in"), (cptr->is_ws ? "" : "NOT "), cptr->conn, cptr->next);
#endif // defined(USE_MONGOOSE)
      i++;
      cptr = cptr->next;
   }
}
// Add a new client to the client list (HTTP or WebSocket)
#ifdef  USE_MONGOOSE
rrconn_t *http_add_client(struct mg_connection *c, bool is_ws) {
   rrconn_t *cptr = (rrconn_t *)malloc( sizeof(rrconn_t) );

   if (!cptr) {
      fprintf(stderr, "OOM in http_add_client\n");

      return NULL;
   }
   memset( cptr, 0, sizeof(rrconn_t) );

   // create some randomness for login hashing and session
   generate_nonce( cptr->token, sizeof(cptr->token) );
   generate_nonce( cptr->nonce, sizeof(cptr->nonce) );
   Log(LOG_CRAZY, "http", "add_client: token:<%p> |%s|, nonce:<%p> |%s|", cptr->token, cptr->token, cptr->nonce,
      cptr->nonce);
   cptr->connected = now;
   cptr->authenticated = false;
   cptr->active = true;
   cptr->conn = c;
   cptr->is_ws = is_ws;

   // Add to the top of the list
   cptr->next = http_client_list;
   http_client_list = cptr;

   Log( LOG_DEBUG, "http", "Added new client at cptr:<%p> (%d clients and %d sessions total now)", cptr,
      http_count_connections(), http_count_clients() );

   return cptr;
}

// Remove a client (WebSocket or HTTP) from the list
void http_remove_client(struct mg_connection *c) {
   if (!c) {
      Log(LOG_CRIT, "http", "http_remove_client passed NULL mg_conn?!");

      return;
   }
   rrconn_t *prev = NULL;
   rrconn_t *current = http_client_list;

   c->is_closing = 1;

   while (current) {
      if (current->conn == c) {
         // Found the client to remove, mark it dead
         current->active = false;

         if (!prev) {
            http_client_list = current->next;
         } else {
            prev->next = current->next;
         }
         Log( LOG_CRAZY, "http", "Removing client at cptr:<%p> with mgconn:<%p> (%d connections / %d users remain)",
            current, c, http_count_connections(), http_count_clients() );

         if (current->user) {
            if (current->authenticated && current->is_ws) {
               current->user->clones--;
            }

            if (current->user->clones < 0) {
               Log(LOG_WARN, "http", "Client at cptr:<%p> has %d clones??", current, current->user->clones);
               current->user->clones = 0;
            }
         }
         memset( current, 0, sizeof(rrconn_t) );
         free(current);

         return;
      }
      prev = current;
      current = current->next;
   }
}
#endif // USE_MONGOOSE

// Counts only websocket clients that are logged in
int http_count_clients(void) {
   int c = 0;
   rrconn_t *cptr = http_client_list;
   while (cptr) {
      if (cptr->authenticated && cptr->is_ws) {
         c++;
      }
      cptr = cptr->next;
   }
   return c;
}

// Counts ALL websocket clients
int http_count_connections(void) {
   int c = 0;
   rrconn_t *cptr = http_client_list;
   while (cptr) {
      c++;
      cptr = cptr->next;
   }
   return c;
}

// Returns the user actively PTTing
rrconn_t *whos_talking(void) {
   rrconn_t *cptr = http_client_list;

   while (cptr) {
      if (cptr->authenticated && cptr->is_ptt) {
         Log(LOG_CRAZY, "http", "whos_talking: returning cptr:<%p> - %s", cptr, cptr->chatname);

         return cptr;
      }
      cptr = cptr->next;
   }
   return NULL;
}
