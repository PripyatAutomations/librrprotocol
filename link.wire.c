//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <librrprotocol/link.wire.h>

static const char *fields[] = {
   "op", "node", "site", "username", "password", "nodes", "parent", "dest", "kind", "session", "payload", "clock.unix", "version", "features", "domain", "identity.public-key", "identity.tls-spki", "identity.signature"
};
static void wire_reason(char *out, size_t cap, const char *fmt, ...) {
   if (!out || !cap) {
      return;
   }
   va_list ap;
   va_start(ap, fmt);
   vsnprintf(out, cap, fmt, ap);
   va_end(ap);
}

static bool valid_ex(dict *d, char *reason, size_t cap) {
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
         wire_reason(reason, cap, "unsupported link.application kind");
         return false;
      }
      required = 1 | (1 << 1) | (1 << 3) | (15 << 7);
   } else {
      wire_reason(reason, cap, "unknown or missing link operation");
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

      if (i == sizeof(fields) / sizeof(fields[0])) {
         wire_reason(reason, cap, "unknown JSON field");
         return false;
      }
      if (dict_get_type(d, key) != VAL_STR || !value) {
         wire_reason(reason, cap, "field %s is not a string", fields[i]);
         return false;
      }
      if (strlen(value) > (i == 5 ? 20000U : i == 10 ? 24000U : 256U)) {
         wire_reason(reason, cap, "field %s exceeds length limit", fields[i]);
         return false;
      }

      if (i == 11) {
         if (strcmp(op, "link.hello") || strlen(value) != 10) {
            wire_reason(reason, cap, "invalid clock.unix field");
            return false;
         }

         for (const char *p = value ; *p ; p++) {
            if (*p < '0' || *p > '9') {
               wire_reason(reason, cap, "invalid clock.unix digits");
               return false;
            }
         }
      }
      seen |= 1U << i;
   }
   /* The clock sample is an optional hello extension; old hellos remain valid. */
   unsigned optional = !strcmp(op, "link.hello") ? (1U << 11) : 0;

   if ((seen & ~optional) != required) {
      for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
         if ((required & (1U << i)) && !(seen & (1U << i))) {
            wire_reason(reason, cap, "missing required field %s", fields[i]);
            return false;
         }
         if ((seen & (1U << i)) && !(required & (1U << i)) && !(optional & (1U << i))) {
            wire_reason(reason, cap, "unexpected field %s for operation", fields[i]);
            return false;
         }
      }
      wire_reason(reason, cap, "invalid operation field set");
      return false;
   }
   return true;
}

static bool valid(dict *d) {
   return valid_ex(d, NULL, 0);
}

dict *rr_link_wire_decode_ex(const char *data, size_t length, char *reason, size_t cap) {
   wire_reason(reason, cap, "unknown decoding error");
   if (!data || !length) {
      wire_reason(reason, cap, "empty link frame");
      return NULL;
   }
   if (length > RR_LINK_MAX_FRAME) {
      wire_reason(reason, cap, "link frame too large");
      return NULL;
   }
   if (memchr(data, 0, length)) {
      wire_reason(reason, cap, "embedded NUL in link frame");
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
      wire_reason(reason, cap, "expected JSON object");
      return NULL;
   }

   char *copy = malloc(length + 1);
   if (!copy) {
      wire_reason(reason, cap, "out of memory");
      return NULL;
   }
   memcpy(copy, data, length);
   copy[length] = '\0';
   dict *d = json2dict(copy);
   free(copy);

   if (!d) {
      wire_reason(reason, cap, "malformed JSON, duplicate key, or trailing content");
      return NULL;
   }
   if (!valid_ex(d, reason, cap)) {
      dict_free(d);
      return NULL;
   }
   wire_reason(reason, cap, "ok");
   return d;
}

dict *rr_link_wire_decode(const char *data, size_t length) {
   return rr_link_wire_decode_ex(data, length, NULL, 0);
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
