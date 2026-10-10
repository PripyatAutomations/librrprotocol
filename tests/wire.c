#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librrprotocol/wire.h>

static unsigned fields(dict *d) {
   unsigned count = 0;
   int rank = 0;
   const char *key;
   dict_value_t value;
   val_type_t type;

   while ((rank = dict_enumerate_typed(d, rank, &key, &value, &type)) >= 0) {
      count++;
   }
   return count;
}

static void equivalent(dict *a, dict *b) {
   assert(a && b && fields(a) == fields(b));
   int rank = 0;
   const char *key;
   dict_value_t value;
   val_type_t type;

   while ((rank = dict_enumerate_typed(a, rank, &key, &value, &type)) >= 0) {
      val_type_t other = dict_get_type(b, key);
      assert(other != VAL_END);

      if (type == VAL_STR) {
         assert(other == VAL_STR && !strcmp(value.s, dict_get(b, key, "")));
      } else if (type == VAL_BOOL || type == VAL_NULL) {
         assert(other == type);

         if (type == VAL_BOOL) {
            assert(dict_get_bool(a, key, false) == dict_get_bool(b, key, false));
         }
      } else {
         assert(dict_get_double(a, key, NAN) == dict_get_double(b, key, NAN));
      }
   }
}

int main(void) {
   FILE *input = fopen("librrprotocol/tests/wire-vectors.jsonl", "r");
   assert(input);
   char *line = NULL;
   size_t capacity = 0;
   unsigned passed = 0;

   while (getline(&line, &capacity, input) >= 0) {
      dict *vector = json2dict(line);
      assert(vector);
      const char *reject = dict_get(vector, "reject", NULL);

      if (reject) {
         assert(!rr_wire_decode(reject));
      } else {
         dict *internal = json2dict(dict_get(vector, "internal", NULL));
         const char *expected = dict_get(vector, "wire", NULL);
         dict *decoded = rr_wire_decode(expected);
         equivalent(internal, decoded);
         char *encoded = rr_wire_encode(internal);
         assert(encoded && strlen(encoded) < strlen(dict_get(vector, "internal", "")));
         dict *actual_wire = json2dict(encoded), *expected_wire = json2dict(expected);
         equivalent(actual_wire, expected_wire);
         dict_free(actual_wire);
         dict_free(expected_wire);
         dict_free(decoded);
         free(encoded);
         dict_free(internal);
      }
      dict_free(vector);
      passed++;
   }
   assert(!ferror(input));
   fclose(input);
   free(line);
   assert(!rr_wire_encode(NULL) && !rr_wire_decode(NULL));
   dict *alias = dict_new();
   dict_add(alias, "msg.type", "auth");
   dict_add(alias, "auth.cmd", "error");
   dict_add(alias, "auth.error", "alias");
   assert(!rr_wire_encode(alias));
   dict_free(alias);
   dict *internal = dict_new();
   dict_add(internal, "msg.type", "property");
   dict_add(internal, "property.cmd", "set");
   dict_add_ullong(internal, "property.value", 9007199254740992ULL);
   assert(!rr_wire_encode(internal));
   dict_add_double(internal, "property.value", INFINITY);
   assert(!rr_wire_encode(internal));
   dict_add_ptr(internal, "property.value", internal);
   assert(!rr_wire_encode(internal));
   dict_free(internal);
   char *large = malloc(65537);
   memset(large, ' ', 65536);
   large[65536] = '\0';
   assert(!rr_wire_decode(large));
   free(large);
   printf("PASS: %u shared compact wire vectors, legacy rejection and numeric/size limits\n", passed);

   return 0;
}
