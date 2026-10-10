// RustyRig compact JSON codec; no application, UI or connection state.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librrprotocol/http.h>
#include <librrprotocol/wire.h>

/* PARITY: rustyrig-www/js/webui.wire.js */
static const char *operations[] = {
   "object.snapshot", "object.unsubscribe", "object.inventory", "object.begin",
   "object.descriptor", "object.added", "object.removed", "object.end",
   "object.result", "object.inventory-entry", "object.inventory-end",
   "property.set", "property.descriptor", "property.state", "property.changed",
   "property.result", NULL
};

static const char *family_for(const char *operation) {
   if (!operation) {
      return NULL;
   }

   for (unsigned i = 0 ; operations[i] ; i++) {
      if (!strcmp(operation, operations[i])) {
         return !strncmp(operation, "object.", 7) ? "object" : "property";
      }
   }

   return NULL;
}

static bool metadata(const char *key) {
   const char *fields[] = {
      "target", "request.id", "request.room", "stream.epoch", "stream.seq",
      "result.code", "inventory.kind", "inventory.name", "inventory.depth",
      "inventory.uuid", "inventory.room", "inventory.backend", "inventory.frequency",
      "inventory.codec", "inventory.direction", "inventory.subsystem",
      "inventory.coordinates", "inventory.source", "inventory.service",
      "inventory.state", "inventory.access", "inventory.action", NULL
   };

   for (unsigned i = 0 ; fields[i] ; i++) {
      if (!strcmp(fields[i], key)) {
         return true;
      }
   }

   return false;
}

static bool payload_field(const char *family, const char *key) {
   const char *object_fields[] = {
      "uuid", "type", "owner", "alias", "name", "lifecycle", "backend", "room", NULL
   };
   const char *property_fields[] = {
      "name", "type", "readable", "writable", "unit", "minimum", "maximum",
      "step", "enum", "observed", "known", "available", "version", "value", NULL
   };
   const char **fields = !strcmp(family, "object") ? object_fields : property_fields;

   for (unsigned i = 0 ; fields[i] ; i++) {
      if (!strcmp(fields[i], key)) {
         return true;
      }
   }

   return false;
}

static bool copy_value(dict *out, const char *key, dict *in, const char *source, val_type_t type, const dict_value_t *v) {
   /* Reject paths already present rather than letting dictionary overwrite resolve ambiguous envelopes. Values are scalars in this initial schema. */
   if (dict_get_type(out, key) != VAL_END) {
      return false;
   }

   if (type != VAL_NULL && type != VAL_STR && type != VAL_BOOL && type != VAL_CHAR) {
      double number = dict_get_double(in, source, NAN);

      if (!isfinite(number) || (trunc(number) == number && fabs(number) > 9007199254740991.0)) {
         return false;
      }
   }
   int error;

   switch (type) {
      case VAL_NULL: {
         error = dict_add_null(out, key);
                                                      break;
      }
      case VAL_STR: {
         error = dict_add(out, key, v->s);
                                                      break;
      }
      case VAL_BOOL: {
         error = dict_add_bool(out, key, v->i);
                                                            break;
      }
      case VAL_CHAR: {
         error = dict_add_char(out, key, v->c);
                                                            break;
      }
      case VAL_INT: {
         error = dict_add_int(out, key, v->i);
                                                          break;
      }
      case VAL_UINT: {
         error = dict_add_uint(out, key, v->ui);
                                                             break;
      }
      case VAL_LONG: {
         error = dict_add_long(out, key, v->l);
                                                            break;
      }
      case VAL_ULONG: {
         error = dict_add_ulong(out, key, v->ul);
                                                               break;
      }
      case VAL_LLONG: {
         error = dict_add_llong(out, key, v->ll);
                                                               break;
      }
      case VAL_ULLONG: {
         error = dict_add_ullong(out, key, v->ull);
                                                                  break;
      }
      case VAL_FLOAT: case VAL_FLOATP: case VAL_DOUBLE: case VAL_DOUBLEP: {
         double number = dict_get_double(in, source, NAN);

         if (!isfinite(number)) {
            return false;
         }
         error = dict_add_double(out, key, number);
         break;
      }
      default: {
         return false;
      }
   }

   return error == 0;
}

static dict *transform(dict *in, bool encode) {
   const char *family;
   char operation[64], command_key[32];

   if (encode) {
      family = dict_get(in, "msg.type", NULL);

      if (!family || (strcmp(family, "object") && strcmp(family, "property"))) {
         return NULL;
      }
      snprintf(command_key, sizeof(command_key), "%s.cmd", family);
      const char *command = dict_get(in, command_key, NULL);

      if (!command || snprintf(operation, sizeof(operation), "%s.%s", family, command) >= sizeof(operation) || !family_for(operation)) {
         return NULL;
      }
   } else {
      const char *op = dict_get(in, "op", NULL);
      family = family_for(op);

      if (!family) {
         return NULL;
      }
      snprintf(operation, sizeof(operation), "%s", op);
      snprintf(command_key, sizeof(command_key), "%s.cmd", family);
   }
   dict *out = dict_new();

   if (!out) {
      return NULL;
   }

   if (encode) {
      if (dict_add(out, "op", operation)) {
         goto invalid;
      }
   } else if (dict_add(out, "msg.type", family) ||
      dict_add(out, command_key, operation + strlen(family) + 1)) {
      goto invalid;
   }
   int rank = 0;
   const char *key;
   dict_value_t value;
   val_type_t type;

   while ((rank = dict_enumerate_typed(in, rank, &key, &value, &type)) >= 0) {
      if (encode && (!strcmp(key, "msg.type") || !strcmp(key, command_key))) {
         continue;
      }

      if (!encode && !strcmp(key, "op")) {
         continue;
      }
      const char *destination = key;
      char path[128];

      if (!metadata(key)) {
         if (encode) {
            size_t prefix = strlen(family);

            if (strncmp(key, family, prefix) || key[prefix] != '.' || !payload_field(family, key + prefix + 1)) {
               goto invalid;
            }
            destination = key + prefix + 1;
         } else {
            if (!payload_field(family, key)) {
               goto invalid;
            }
            snprintf(path, sizeof(path), "%s.%s", family, key);
            destination = path;
         }
      }

      if (!copy_value(out, destination, in, key, type, &value)) {
         goto invalid;
      }
   }
   return out;
invalid:
   dict_free(out);

   return NULL;
}

char *rr_wire_encode(dict *message) {
   dict *wire = transform(message, true);

   if (!wire) {
      return NULL;
   }
   char *json = dict2json(wire);
   dict_free(wire);

   if (json && strlen(json) > HTTP_WS_MAX_MSG) {
      free(json);
      json = NULL;
   }

   return json;
}

/* Field names are literal ASCII identifiers, never dotted/escaped aliases. Empty containers and arrays have no meaning in this scalar initial schema. JSON
 * syntax/duplicate checks remain owned by json2dict(). */
static bool literal_keys(const char *json) {
   for (const char *p = json ; *p ; p++) {
      if (*p == '[') {
         return false;
      }

      if (*p == '{') {
         const char *q = p + 1;
         while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') {
            q++;
         }

         if (*q == '}') {
            return false;
         }
      }

      if (*p != '"') {
         continue;
      }
      const char *start = ++p;
      while (*p && *p != '"') {
         if (*p == '\\') {
            if (!p[1]) {
               return false;
            }
            p++;
         }
         p++;
      }

      if (!*p) {
         return false;
      }
      const char *next = p + 1;
      while (*next == ' ' || *next == '\t' || *next == '\r' || *next == '\n') {
         next++;
      }

      if (*next == ':') {
         if (p == start || p - start > 63 || *start < 'a' || *start > 'z') {
            return false;
         }

         for (const char *q = start ; q < p ; q++) {
            if (!(*q >= 'a' && *q <= 'z') && !(*q >= '0' && *q <= '9') && *q != '_' && *q != '-') {
               return false;
            }
         }

         if ((p - start == 3 && !memcmp(start, "msg", 3)) ||
            (p - start == 3 && !memcmp(start, "cmd", 3)) ||
            (p - start == 6 && !memcmp(start, "object", 6)) ||
            (p - start == 8 && !memcmp(start, "property", 8))) {
            return false;
         }
      }
   }

   return true;
}

dict *rr_wire_decode(const char *json) {
   if (!json || strlen(json) > HTTP_WS_MAX_MSG) {
      return NULL;
   }
   const char *root = json;
   while (*root == ' ' || *root == '\t' || *root == '\r' || *root == '\n') {
      root++;
   }

   if (*root != '{' || !literal_keys(root)) {
      return NULL;
   }
   dict *wire = json2dict(json);
   dict *message = wire ? transform(wire, false) : NULL;
   dict_free(wire);

   return message;
}
