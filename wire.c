// RustyRig compact JSON codec; no application, UI or connection state.
//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.

#include <math.h>
#include <stdio.h>
#include <stdarg.h>
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
      case VAL_FLOAT:
      case VAL_FLOATP:
      case VAL_DOUBLE:
      case VAL_DOUBLEP: {
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

/* Logs schema metadata only: never serialize rejected payloads or credentials. */
static const char *wire_safe_label(const char *src, char *dst, size_t size) {
   if (!src || !*src) {
      return "(missing)";
   }
   size_t i = 0;

   for ( ; src[i] && i + 1 < size ; i++) {
      unsigned char c = (unsigned char)src[i];

      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) {
         return "(invalid)";
      }
      dst[i] = (char)c;
   }

   if (src[i]) {
      return "(too-long)";
   }
   dst[i] = '\0';

   return dst;
}
static dict *transform(dict *in, bool encode, const rr_wire_rule_t **selected) {
   const rr_wire_rule_t *rule = rule_for(in, encode);

   if (selected) {
      *selected = rule;
   }

   if (!rule) {
      if (encode) {
         Log(LOG_WARN, "wire", "Unsupported outgoing operation: %s", dict_get(in, "msg.type", "missing"));
      } else {
         char op[72];
         Log(LOG_WARN, "wire", "Rejected incoming message: unsupported op=%s", wire_safe_label(dict_get(in, "op", NULL), op, sizeof(op)));
      }

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

      if (!field_path(rule, key, encode, path, sizeof(path)) || !copy_value(out, path, in, key, type, &value)) {
         if (encode) {
            Log(LOG_WARN, "wire", "Invalid outgoing field %s for %s", key, rule->operation);
         } else {
            char field[72];
            Log(LOG_WARN, "wire", "Rejected incoming %s: unmapped or invalid field=%s (type/schema mismatch)", rule->operation, wire_safe_label(key, field,
               sizeof(field)));
         }
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
   dict *wire = transform(message, true, NULL);

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
            (p - start == 6 && !memcmp(start, "object", 6)) || (p - start == 8 && !memcmp(start, "property", 8))) {
            return false;
         }
      }
   }

   return true;
}

static dict *wire_decode(const char *json, size_t length, const rr_wire_rule_t **selected) {
   if (!json || length > HTTP_WS_MAX_MSG) {
      Log(LOG_WARN, "wire", "Rejected incoming message: %s (bytes=%zu, maximum=%u)", !json ? "null frame" : "frame too large", length, (unsigned)HTTP_WS_MAX_MSG
         );

      return NULL;
   }
   const char *root = json;
   while (*root == ' ' || *root == '\t' || *root == '\r' || *root == '\n') {
      root++;
   }

   if (*root != '{') {
      Log(LOG_WARN, "wire", "Rejected incoming message: expected JSON object");

      return NULL;
   }

   if (!literal_keys(root)) {
      Log(LOG_WARN, "wire", "Rejected incoming message: invalid JSON key, container, or reserved field");

      return NULL;
   }
   dict *wire = json2dict(json);

   if (!wire) {
      Log(LOG_WARN, "wire", "Rejected incoming message: malformed JSON or duplicate/invalid fields");
   }
   dict *message = wire ? transform(wire, false, selected) : NULL;
   dict_free(wire);

   return message;
}

#ifdef USE_PROFILING
#include <pthread.h>
#define WIRE_PROFILE_INVALID (sizeof(wire_rules) / sizeof(wire_rules[0]))
#define WIRE_PROFILE_JSON_ROWS (WIRE_PROFILE_INVALID + 1)
#define WIRE_PROFILE_ROWS (WIRE_PROFILE_JSON_ROWS + 9)
static const char *binary_labels[] = {
   "binary/unknown", "binary/audio", "binary/video", "binary/waterfall", "binary/modem",
   "binary/file", "binary/control", "binary/log", "binary/keepalive"
};

typedef struct {
   uint64_t count, rejected, bytes, parse_us;
} wire_profile_count_t;
typedef struct {
   const char *operation;
   wire_profile_count_t counts;
} wire_profile_row_t;
static wire_profile_count_t wire_profile_counts[WIRE_PROFILE_ROWS];
static pthread_mutex_t wire_profile_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t wire_profile_started, wire_profile_last_dump;
static bool wire_profile_has_dumped;

static int wire_profile_compare(const void *a, const void *b) {
   const wire_profile_row_t *left = a, *right = b;

   if (left->counts.count != right->counts.count) {
      return left->counts.count < right->counts.count ? 1 : -1;
   }

   return strcmp(left->operation, right->operation);
}
#endif

/* Process-local parser instrumentation: fixed registry rows, no payload data or attacker-controlled labels. Snapshot before logging so log forwarding/re-entry
 * cannot deadlock or change the rows being printed. */
/* Dynamically grow profiling reports so one Log() call owns each dump. */
static bool profile_report_append(char **text, size_t *used, size_t *capacity, const char *format, ...) {
   va_list args, measure;
   va_start(args, format);
   va_copy(measure, args);
   int needed = vsnprintf(NULL, 0, format, measure);
   va_end(measure);

   if (needed < 0 || (size_t)needed > SIZE_MAX - *used - 1) {
      va_end(args);

      return false;
   }
   size_t required = *used + (size_t)needed + 1;

   if (required > *capacity) {
      size_t grown = *capacity ? *capacity : 512;
      while (grown < required) {
         if (grown > SIZE_MAX / 2) {
            grown = required;
            break;
         }
         grown *= 2;
      }
      char *replacement = realloc(*text, grown);

      if (!replacement) {
         va_end(args);

         return false;
      }
      *text = replacement;
      *capacity = grown;
   }
   vsnprintf(*text + *used, *capacity - *used, format, args);
   *used += (size_t)needed;
   va_end(args);

   return true;
}

void rr_wire_profile_dump(void) {
#ifdef USE_PROFILING
   wire_profile_row_t rows[WIRE_PROFILE_ROWS];
   size_t used = 0;
   uint64_t current = mono_us();
   pthread_mutex_lock(&wire_profile_lock);

   if (!wire_profile_started || (wire_profile_has_dumped && current - wire_profile_last_dump < UINT64_C(300000000))) {
      pthread_mutex_unlock(&wire_profile_lock);

      return;
   }
   wire_profile_last_dump = current;
   wire_profile_has_dumped = true;
   uint64_t elapsed = current - wire_profile_started;

   for (size_t i = 0 ; i < WIRE_PROFILE_ROWS ; i++) {
      if (wire_profile_counts[i].count) {
         rows[used].operation = i >= WIRE_PROFILE_JSON_ROWS ? binary_labels[i - WIRE_PROFILE_JSON_ROWS] :
            i == WIRE_PROFILE_INVALID ? "<invalid/unknown>" : wire_rules[i].operation;
         rows[used++].counts = wire_profile_counts[i];
      }
   }

   pthread_mutex_unlock(&wire_profile_lock);
   qsort(rows, used, sizeof(*rows), wire_profile_compare);
   char *duration = time_t2dhms(elapsed / 1000000);
   char *report = NULL;
   size_t report_len = 0, report_capacity = 0;
   bool report_ok = profile_report_append(&report, &report_len, &report_capacity, "WebSocket JSON parser / binary dispatch counts (cumulative, sampling=%s):",
      duration ? duration : "unknown");
   free(duration);
   size_t json_rows = 0;

   for (size_t i = 0 ; i < used ; i++) {
      if (strncmp(rows[i].operation, "binary/", 7) && json_rows++ >= 32) {
         continue;
      }
      const wire_profile_count_t *c = &rows[i].counts;
      report_ok = report_ok
         && profile_report_append(&report, &report_len, &report_capacity, "\n  %s: %llu messages, %llu bytes, %llu rejected, %llu processing us (%llu avg us)",
         rows[i].operation, (unsigned long long)c->count, (unsigned long long)c->bytes, (unsigned long long)c->rejected, (unsigned long long)c->parse_us, (
            unsigned long long)(c->parse_us / c->count));
   }

   if (report_ok) {
      Log(LOG_INFO, "profile", "%s", report ? report : "");
   }
   free(report);

#endif
}

bool rr_wire_profile_summary(rr_wire_profile_summary_t *out) {
   if (!out) {
      return false;
   }
   memset(out, 0, sizeof(*out));
#ifdef USE_PROFILING
   pthread_mutex_lock(&wire_profile_lock);

   for (size_t i = 0 ; i < WIRE_PROFILE_ROWS ; i++) {
      const wire_profile_count_t *p = &wire_profile_counts[i];
      out->messages += p->count;
      out->bytes += p->bytes;
      out->rejected += p->rejected;
      out->processing_us += p->parse_us;

      if (i >= WIRE_PROFILE_JSON_ROWS) {
         out->binary_messages += p->count;
      } else {
         out->json_messages += p->count;
      }
   }

   pthread_mutex_unlock(&wire_profile_lock);

   return true;
#else

   return false;
#endif
}

void rr_wire_profile_binary(const void *data, size_t length, bool accepted, uint64_t elapsed_us) {
#ifdef USE_PROFILING
   const unsigned char *frame = data;
   unsigned subsystem = length >= 4 && frame && frame[0] == 'R' && frame[1] == 'R' ? frame[3] : 0;
   unsigned bucket = subsystem == 255 ? 8 : subsystem <= 7 ? subsystem : 0;
   uint64_t current = mono_us();
   pthread_mutex_lock(&wire_profile_lock);

   if (!wire_profile_started) {
      wire_profile_started = current;
   }
   wire_profile_count_t *counts = &wire_profile_counts[WIRE_PROFILE_JSON_ROWS + bucket];
   counts->count++;
   counts->bytes += length;
   counts->rejected += !accepted;
   counts->parse_us += elapsed_us;
   pthread_mutex_unlock(&wire_profile_lock);
   rr_wire_profile_dump();
#else
   (void)data;
   (void)length;
   (void)accepted;
   (void)elapsed_us;
#endif
}

dict *rr_wire_decode(const char *json) {
#ifdef USE_PROFILING
   uint64_t start = mono_us();
   const rr_wire_rule_t *rule = NULL;
   size_t length = json ? strlen(json) : 0;
   dict *message = wire_decode(json, length, &rule);
   uint64_t elapsed = mono_us() - start;
   size_t row = rule ? (size_t)(rule - wire_rules) : WIRE_PROFILE_INVALID;
   pthread_mutex_lock(&wire_profile_lock);

   if (!wire_profile_started) {
      wire_profile_started = start;
   }
   wire_profile_count_t *counts = &wire_profile_counts[row];
   counts->count++;
   counts->rejected += message == NULL;
   counts->bytes += length;
   counts->parse_us += elapsed;
   pthread_mutex_unlock(&wire_profile_lock);
   rr_wire_profile_dump();

   return message;
#else

   return wire_decode(json, json ? strlen(json) : 0, NULL);
#endif
}
