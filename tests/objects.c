#include <assert.h>
#include <limits.h>
#include <string.h>
#include <librrprotocol/objects.h>
time_t now;
bool dying, restarting;
int main(void) {
   assert(rr_object_uuid_valid("01234567-89ab-4def-8123-456789abcdef"));
   assert(!rr_object_uuid_valid("rig0"));
   assert(!rr_object_uuid_valid(NULL));
   dict *d = dict_new();
   rr_object_seq_put(d, "version", UINT64_MAX);
   char *json = dict2json(d);
   dict *parsed = json2dict(json);
   uint64_t version;
   assert(rr_object_seq_get(parsed, "version", &version) && version == UINT64_MAX);
   dict_value_t value = {
      .l = 145000000
   }, decoded;
   assert(rr_object_value_put(d, "value", VAL_LONG, &value));
   assert(rr_object_value_get(d, "value", VAL_LONG, &decoded) && decoded.l == value.l);
   dict_add(d, "value", "145000000");
   assert(!rr_object_value_get(d, "value", VAL_LONG, &decoded));
   dict_add_double(d, "value", 1.5);
   assert(!rr_object_value_get(d, "value", VAL_LONG, &decoded));
   dict_add_bool(d, "value", true);
   assert(!rr_object_value_get(d, "value", VAL_INT, &decoded));
   dict_add_long(d, "value", -1);
   assert(!rr_object_value_get(d, "value", VAL_UINT, &decoded));
   value.ull = UINT64_MAX;
   assert(!rr_object_value_put(d, "value", VAL_ULLONG, &value));
   dict_add(d, "version", "18446744073709551616");
   assert(!rr_object_seq_get(d, "version", &version));
   rrconn_t client = {
      0
   };
   assert(!rr_object_server_request(&client, d));
   free(json);
   dict_free(parsed);
   dict_free(d);
   puts("PASS: UUID and typed wire validation, exact sequence encoding, authentication gate");
}
