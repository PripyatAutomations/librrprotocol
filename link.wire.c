#include <stdlib.h>
#include <string.h>
#include "build_config.h"
#ifdef USE_MONGOOSE
#include <ext/libmongoose/mongoose.h>
#endif
#include <librrprotocol/link.wire.h>

static const char *fields[] = {
   "op", "node", "site", "root", "username", "password", "role", "nodes", "parent", "dest", "kind", "session", "payload"
};
static bool valid(dict *d) {
   const char *op = dict_get(d, "op", "");
   unsigned required;

   if (!strcmp(op, "link.hello")) {
      required = 127;
   } else if (!strcmp(op, "link.snapshot")) {
      required = 1 | (1 << 7);
   } else if (!strcmp(op, "link.ready")) {
      required = 1;
   } else if (!strcmp(op, "link.introduce")) {
      required = 1 | (1 << 1) | (1 << 8);
   } else if (!strcmp(op, "link.withdraw")) {
      required = 1 | (1 << 1);
   } else if (!strcmp(op, "link.application")) {
      const char *kind = dict_get(d, "kind", "");
      const char *kinds[] = {
         "catalog", "catalog-remove", "users", "request", "reply", "state", "subscribe", "unsubscribe", "close"
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
      required = 1 | (1 << 1) | (1 << 4) | (15 << 9);
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

      if (i == sizeof(fields) / sizeof(fields[0]) || dict_get_type(d, key) != VAL_STR || !value || strlen(value) > (i == 7 ? 20000U : i == 12 ? 24000U : 256U))
      {
         return false;
      }
      seen |= 1U << i;
   }
   return seen == required;
}

dict *rr_link_wire_decode(const char *data, size_t length) {
#ifdef USE_MONGOOSE

   if (!data || !length || length > RR_LINK_MAX_FRAME || memchr(data, 0, length)) {
      return NULL;
   }
   struct mg_str json = {
      (char *)data, length
   }, key, value;
   int token_length;
   int start = mg_json_get(json, "$", &token_length);

   if (start < 0 || data[start] != '{') {
      return NULL;
   }

   for (size_t i = start + token_length ; i < length ; i++) {
      if (!strchr(" \t\r\n", data[i])) {
         return NULL;
      }
   }

   unsigned seen = 0;
   size_t offset = 0;
   while ((offset = mg_json_next(json, offset, &key, &value))) {
      unsigned i;

      for (i = 0 ; i < sizeof(fields) / sizeof(fields[0]) ; i++) {
         size_t n = strlen(fields[i]);

         if (key.len == n + 2 && key.buf[0] == '"' && key.buf[n + 1] == '"' && !memcmp(key.buf + 1, fields[i], n)) {
            break;
         }
      }

      if (i == sizeof(fields) / sizeof(fields[0]) || (seen & (1U << i)) || value.len < 2 || value.buf[0] != '"') {
         return NULL;
      }
      seen |= 1U << i;

      /* No embedded NUL or escape ambiguity in link identity/credential fields. */
      for (size_t j = 1 ; j + 1 < value.len ; j++) {
         if (value.buf[j] == '\\') {
            if (j + 5 < value.len && !memcmp(value.buf + j, "\\u0000", 6)) {
               return NULL;
            }
            j++;                // An escaped backslash must not become a second Unicode escape.
         }
      }
   }
   char *copy = malloc(length + 1);

   if (!copy) {
      return NULL;
   }
   memcpy(copy, data, length);
   copy[length] = 0;
   dict *d = json2dict(copy);
   free(copy);

   if (d && !valid(d)) {
      dict_free(d);
      d = NULL;
   }

   return d;
#else
   (void)data;
   (void)length;

   return NULL;
#endif
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
