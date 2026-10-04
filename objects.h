//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// UUID object/property wire contract. No server or UI state belongs here.
#ifndef RR_PROTOCOL_OBJECTS_H
#define RR_PROTOCOL_OBJECTS_H
#include <librustyaxe/core.h>
/* Wire contract (dotted dict keys serialize as nested JSON):
 * msg.type=object, object.cmd=snapshot/unsubscribe (requests),
 * begin/descriptor/added/removed/end/result (responses/events).
 * object.cmd=inventory requests one-shot discovery, with inventory-entry and
 * inventory-end replies (request.id correlated; independent of stream/cache).
 * See doc/resource-discovery.md in rustyrig-fw for inventory fields.
 * msg.type=property, property.cmd=set (request),
 * descriptor/state/changed/result (responses/events).
 * Objects: object.uuid/type/owner/alias/name/lifecycle[/backend].
 * Properties: target UUID + property.name, never a VFO alias path.
 * Schema: property.type/readable/writable[/unit/minimum/maximum/step/enum].
 * State: property.observed/known/available/version[/value].
 * Control: property.value; response: result.code. request.id correlates.
 * Server messages: stream.epoch UUID, stream.seq uint64 decimal string;
 * property.version is also a decimal string, compared per target/property.
 * Full schemas, consistency and error codes: doc/object-property-protocol.md
 * in rustyrig-fw. No new UI or backend state belongs in this library.
 */
#define RR_OBJECT_REQUEST_EVENT "protocol.object.request"
#define RR_OBJECT_MESSAGE_EVENT "protocol.object.message"
#define RR_OBJECT_CLOSE_EVENT "protocol.object.close"
bool rr_object_uuid_valid(const char *uuid);
bool rr_object_name_valid(const char *name);
const char *rr_object_type_name(val_type_t type);
bool rr_object_value_put(dict *d, const char *key, val_type_t type,
   const dict_value_t *value);
bool rr_object_value_get(dict *d, const char *key, val_type_t type,
   dict_value_t *value);
void rr_object_seq_put(dict *d, const char *key, uint64_t seq);
bool rr_object_seq_get(dict *d, const char *key, uint64_t *seq);
bool rr_object_server_request(rrconn_t *cptr, dict *d);
bool rr_object_client_message(rrconn_t *cptr, dict *d);
#endif
