//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#include <librrprotocol/objects.h>
#include <librrprotocol/rrprotocol.h>

bool rr_object_uuid_valid(const char *uuid) {
   if (!uuid || strlen(uuid) != 36) { return false; }

   for (int i = 0 ; i < 36 ; i++) {
      if (i == 8 || i == 13 || i == 18 || i == 23) {
         if (uuid[i] != '-') { return false; }
      } else if ( !isdigit( (unsigned char)uuid[i] ) &&
                  !(uuid[i] >= 'a' && uuid[i] <= 'f') ) { return false; }
   }

   return true;
}

bool rr_object_name_valid(const char *name) {
   if (!name || !*name || strlen(name) >= 64) { return false; }

   for (const unsigned char *p = (const unsigned char *)name ; *p ; p++) {
      if (!isalnum(*p) && *p != '.' && *p != '_' && *p != '-') { return false; }
   }

   return true;
}

const char *rr_object_type_name(val_type_t type) {
   switch (type) {
      case VAL_STR: {
         return "string";
      }
      case VAL_BOOL: {
         return "boolean";
      }
      case VAL_INT: case VAL_UINT: case VAL_LONG: case VAL_ULONG:
      case VAL_LLONG: case VAL_ULLONG: case VAL_CHAR: {
         return "integer";
      }
      case VAL_FLOAT: case VAL_DOUBLE: {
         return "number";
      }
      default: {
         return NULL;
      }
   }
}

void rr_object_seq_put(dict *d, const char *key, uint64_t seq) {
   char text[32];
   snprintf(text, sizeof(text), "%" PRIu64, seq);
   dict_add(d, key, text);
}

bool rr_object_seq_get(dict *d, const char *key, uint64_t *seq) {
   if (dict_get_type(d, key) != VAL_STR) { return false; }
   const char *s = dict_get(d, key, "");

   if ( !*s || (s[0] == '0' && s[1]) ) { return false; }

   for (const char *p = s ; *p ; p++) {
      if (*p < '0' || *p > '9') {
         return false;
      }
   }

   errno = 0;
   char *end;
   unsigned long long n = strtoull(s, &end, 10);

   if (errno || *end) { return false; }
   *seq = n;

   return true;
}

// JSON integers are restricted to the exact cross-client range (2^53-1).
// Sequences use decimal strings instead and retain the full uint64 range.
bool rr_object_value_put(dict *d, const char *key, val_type_t type, const dict_value_t *v) {
   if (!d || !v) { return false; }
   long long n;

   switch (type) {
      case VAL_STR: {
         return v->s && !dict_add(d, key, v->s);
      }
      case VAL_BOOL: {
         return !dict_add_bool(d, key, v->i != 0);
      }
      case VAL_FLOAT: {
         return isfinite(v->f) && !dict_add_double(d, key, v->f);
      }
      case VAL_DOUBLE: {
         return isfinite(v->d) && !dict_add_double(d, key, v->d);
      }
      case VAL_INT: {
         n = v->i; break;
      }
      case VAL_UINT: {
         n = v->ui; break;
      }
      case VAL_LONG: {
         n = v->l; break;
      }
      case VAL_LLONG: {
         n = v->ll; break;
      }
      case VAL_CHAR: {
         n = v->c; break;
      }
      case VAL_ULONG: {
         if (v->ul > 9007199254740991ULL) { return false; }
         n = v->ul; break;
      }
      case VAL_ULLONG: {
         if (v->ull > 9007199254740991ULL) { return false; }
         n = v->ull; break;
      }
      default: {
         return false;
      }
   }

   return n >= -9007199254740991LL && n <= 9007199254740991LL &&
          !dict_add_llong(d, key, n);
}

bool rr_object_value_get(dict *d, const char *key, val_type_t type, dict_value_t *v) {
   val_type_t actual = dict_get_type(d, key);
   memset( v, 0, sizeof(*v) );

   if (type == VAL_STR) {
      v->s = actual == VAL_STR ? dict_get(d, key, NULL) : NULL;

      return v->s != NULL;
   }

   if (type == VAL_BOOL) {
      v->i = dict_get_bool(d, key, false);

      return actual == VAL_BOOL;
   }
   bool integer = actual == VAL_INT || actual == VAL_UINT ||
                  actual == VAL_LONG || actual == VAL_ULONG || actual == VAL_LLONG ||
                  actual == VAL_ULLONG;

   if (!integer && actual != VAL_DOUBLE && actual != VAL_FLOAT) { return false; }
   double number = dict_get_double(d, key, NAN);

   if ( !isfinite(number) ) { return false; }

   if (type == VAL_DOUBLE) { v->d = number; return true; }

   if (type == VAL_FLOAT) { v->f = number; return isfinite(v->f); }

   if (!integer || number < -9007199254740991.0 || number > 9007199254740991.0) {
      return false;
   }
   long long n = dict_get_llong(d, key, 0);

   switch (type) {
      case VAL_INT: {
         if (n < INT_MIN || n > INT_MAX) { return false; }
         v->i = n; break;
      }
      case VAL_UINT: {
         if (n < 0 || n > UINT_MAX) { return false; }
         v->ui = n; break;
      }
      case VAL_LONG: {
         if (n < LONG_MIN || n > LONG_MAX) { return false; }
         v->l = n; break;
      }
      case VAL_ULONG: {
         if (n < 0 || (unsigned long long)n > ULONG_MAX) { return false; }
         v->ul = n; break;
      }
      case VAL_LLONG: {
         v->ll = n; break;
      }
      case VAL_ULLONG: {
         if (n < 0) { return false; }
         v->ull = n; break;
      }
      case VAL_CHAR: {
         if (n < CHAR_MIN || n > CHAR_MAX) { return false; }
         v->c = n; break;
      }
      default: {
         return false;
      }
   }

   return true;
}

bool rr_object_server_request(rrconn_t *cptr, dict *d) {
   if (!cptr || !cptr->authenticated || !d) { return false; }
   event_emit_dict(RR_OBJECT_REQUEST_EVENT, cptr, d);

   return true;
}

bool rr_object_client_message(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) { return false; }
   event_emit_dict(RR_OBJECT_MESSAGE_EVENT, cptr, d);

   return true;
}
