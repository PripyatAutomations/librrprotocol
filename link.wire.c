//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdlib.h>
#include <string.h>
#include "build_config.h"
#ifdef USE_MONGOOSE
#include <ext/libmongoose/mongoose.h>
#endif
#include <librrprotocol/link.wire.h>

static const char *fields[] = {
   "op", "node", "site", "username", "password", "nodes", "parent", "dest", "kind", "session", "payload", "clock.unix", "version", "features", "domain", "identity.public-key", "identity.tls-spki", "identity.signature"
};
static bool valid(dict *d) {
   const char *op = dict_get(d, "op", "");
   unsigned required;

   if (!strcmp(op, "link.hello")) {
      required = 1 | (1 << 1) | (1 << 2) | (1 << 4) | (1 << 14);
   } else if (!strcmp(op, "link.identity")) {
      required = 1 | (1U << 1) | (1U << 14) | (1U << 15) | (1U << 16) | (1U << 17);
   } else if (!strcmp(op, "link.snapshot")) {
      required = 1 | (1 << 5);
   } else if (!strcmp(op, "link.capabilities")) {
      required = 1 | (1 << 12) | (1 << 13);
   } else if (!strcmp(op, "link.ready")) {
      required = 1;
   } else if (!strcmp(op, "link.introduce")) {
      required = 1 | (1 << 1) | (1 << 2) | (1 << 6);
   } else if (!strcmp(op, "link.withdraw")) {
      required = 1 | (1 << 1);
   } else if (!strcmp(op, "link.application")) {
      const char *kind = dict_get(d, "kind", "");
      const char *kinds[] = {
         "catalog", "catalog-remove", "catalog-begin", "catalog-end", "catalog-ack", "users", "request", "reply", "state", "subscribe", "unsubscribe", "close"
      };
      bool known = false;

      for (size_t i = 0 ; i < sizeof(kinds) / sizeof(kinds[0]) ; i++) {
         if (!strcmp(kind, kinds[i])) {
            known = true;
         }
      }

      if (!known) {
         return false;
      }
      required = 1 | (1 << 1) | (1 << 3) | (15 << 7);
   } else {
      return false;
   }
   unsigned seen = 0;
   int rank = 0;
   const char *key;
   char *value;
   while ((rank = dict_enumerate(d, rank, &key, &value)) >= 0) {
      unsigned i;

      for (i = 0 ; i < sizeof(fields) / sizeof(fields[0]) ; i++) {
         if (!strcmp(key, fields[i])) {
            break;
         }
      }

      if (i == sizeof(fields) / sizeof(fields[0]) || dict_get_type(d, key) != VAL_STR || !value || strlen(value) > (i == 5 ? 20000U : i == 10 ? 24000U : 256U))
      {
         return false;
      }

      if (i == 11) {
         if (strcmp(op, "link.hello") || strlen(value) != 10) {
            return false;
         }

         for (const char *p = value ; *p ; p++) {
            if (*p < '0' || *p > '9') {
               return false;
            }
         }
      }
      seen |= 1U << i;
   }
   /* The clock sample is an optional hello extension; old hellos remain valid. */
   unsigned optional = !strcmp(op, "link.hello") ? (1U << 11) : 0;

   return (seen & ~optional) == required;
}

dict *rr_link_wire_decode(const char *data, size_t length) {
   if (!data || !length || length > RR_LINK_MAX_FRAME || memchr(data, 0, length)) {
      return NULL;
   }

   /* The link protocol uses a single flat JSON object. Parsing and strict
    * field validation must not depend on the chosen TLS/WebSocket backend.
    * json2dict rejects duplicate keys, malformed JSON and trailing data. */
   size_t start = 0;
   while (start < length && strchr(" \t\r\n", data[start])) {
      start++;
   }
   if (start == length || data[start] != '{') {
      return NULL;
   }

   char *copy = malloc(length + 1);
   if (!copy) {
      return NULL;
   }
   memcpy(copy, data, length);
   copy[length] = '\0';
   dict *d = json2dict(copy);
   free(copy);

   if (d && !valid(d)) {
      dict_free(d);
      d = NULL;
   }
   return d;
}

char *rr_link_wire_encode(dict *message) {
   if (!message || !valid(message)) {
      return NULL;
   }
   char *wire = dict2json(message);

   if (wire && strlen(wire) > RR_LINK_MAX_FRAME) {
      free(wire);

      return NULL;
   }

   return wire;
}
