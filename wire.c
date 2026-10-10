// RustyRig compact JSON codec; no application, UI or connection state.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librrprotocol/http.h>
#include <librrprotocol/wire.h>

typedef struct {
   const char *internal, *wire;
} rr_wire_field_t;
typedef struct {
   const char *operation, *type, *command_key, *command;
   const rr_wire_field_t *fields;
   size_t field_count;
} rr_wire_rule_t;
/* PARITY: rustyrig-www/js/webui.wire.registry.js */
#include <librrprotocol/wire-registry.h>

static const rr_wire_rule_t *rule_for(dict *in, bool encode) {
   const char *identity = dict_get(in, encode ? "msg.type" : "op", NULL);
   if (!identity) {
      return NULL;
   }
   for (unsigned i = 0 ; i < sizeof(wire_rules) / sizeof(wire_rules[0]) ; i++) {
      const rr_wire_rule_t *rule = &wire_rules[i];
      if (strcmp(identity, encode ? rule->type : rule->operation)) {
         continue;
      }
      if (!encode) {
         return rule;
      }
      const char *command = rule->command_key ? dict_get(in, rule->command_key, NULL) : NULL;
      if ((!command && !rule->command) || (command && rule->command && !strcmp(command, rule->command))) {
         return rule;
      }
   }

   return NULL;
}

static bool field_path(const rr_wire_rule_t *rule, const char *key, bool encode, char *path, size_t capacity) {
   for (unsigned i = 0 ; i < rule->field_count ; i++) {
      const char *from = encode ? rule->fields[i].internal : rule->fields[i].wire;
      const char *to = encode ? rule->fields[i].wire : rule->fields[i].internal;
      size_t length = strlen(from);
      if (length && from[length - 1] == '*') {
         if (strncmp(key, from, length - 1) || !key[length - 1] || strchr(key + length - 1, '.')) {
            continue;
         }
         return snprintf(path, capacity, "%.*s%s", (int)strlen(to) - 1, to, key + length - 1) < (int)capacity;
      }
      if (!strcmp(from, key)) {
         return snprintf(path, capacity, "%s", to) < (int)capacity;
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
   const rr_wire_rule_t *rule = rule_for(in, encode);
   if (!rule) {
      if (encode) Log(LOG_WARN, "wire", "Unsupported outgoing operation: %s", dict_get(in, "msg.type", "missing"));
      return NULL;
   }
   dict *out = dict_new();
   if (!out) {
      return NULL;
   }
   if (encode ? dict_add(out, "op", rule->operation) :
       (dict_add(out, "msg.type", rule->type) || (rule->command && dict_add(out, rule->command_key, rule->command)))) {
      goto invalid;
   }
   int rank = 0;
   const char *key;
   dict_value_t value;
   val_type_t type;
   while ((rank = dict_enumerate_typed(in, rank, &key, &value, &type)) >= 0) {
      if (encode ? (!strcmp(key, "msg.type") || (rule->command_key && !strcmp(key, rule->command_key))) : !strcmp(key, "op")) {
         continue;
      }
      char path[128];
      if (!field_path(rule, key, encode, path, sizeof(path)) ||
          !copy_value(out, path, in, key, type, &value)) {
         if (encode) Log(LOG_WARN, "wire", "Invalid outgoing field %s for %s", key, rule->operation);
         goto invalid;
      }
   }

   return out;
invalid:
   dict_free(out);

   return NULL;
}

static bool literal_keys(const char *json);

char *rr_wire_encode(dict *message) {
   dict *wire = transform(message, true);

   if (!wire) {
      return NULL;
   }
   char *json = dict2json(wire);
   dict_free(wire);

   if (json && (strlen(json) > HTTP_WS_MAX_MSG || !literal_keys(json))) {
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
