# librrprotocol C API guide (v1.0 draft)

librrprotocol supplies the RustyRig client/server wire protocol, media
negotiation, transport integration, room control, and SDR bridge interfaces.
Public declarations live in `librrprotocol/*.h`. Include only headers you use;
`rrprotocol.h` is a convenience umbrella for legacy consumers.

## Common conventions

- Operation-returning `bool`: **true means success**, **false means failure**.
- Predicate-returning `bool`: true means the named condition holds.
- `dict_add*` uses **0 on success**, nonzero on failure.
- Owned pointer returns must be released by the caller. Borrowed pointers to
  registered drivers, connection objects, or process-global state must not be
  freed and must not outlive their owner.
- Configuration section callbacks return true after successfully handling a
  line. The generic parser and callback dispatcher live in librustyaxe;
  subsystem parsing belongs in `cfg.*.c` source files.

## Wire protocol (`wire.h`)

`rr_wire_encode(dict *)` creates a JSON string; free the result.
`rr_wire_decode(const char *)` creates a dictionary; free it.
Dotted internal keys serialize to nested JSON. Serialization boundaries must
not silently change value types, especially identifiers.

## Objects (`objects.h`)

`rr_object_uuid_valid` and `rr_object_name_valid` are predicates.
`rr_object_value_put` and `rr_object_value_get` return true on successful
typed conversion/storage. `rr_object_seq_get` returns true when a decimal
sequence is parsed. The sequence format intentionally remains a decimal
string, not a JSON number: JavaScript peers cannot represent arbitrary
unsigned 64-bit values exactly.

`rr_object_seq_put` currently returns `void`, so dictionary allocation
failure is not exposed; a checked setter is a potential API refinement before
the 1.0 freeze.

## Codecs (`codecneg.h`)

`codec_filter_common()` and `codec_filter_test_mode()` allocate returned
strings; free them. `codec_is_test_variant()` is a predicate.
`media_capab_prepare()` returns a dictionary owned by the caller.

## SDR bridge plugins (`bridge.sdr.h`, `bridge.config.h`)

`rrp_sdr_register()` returns true on successful registration.
`rrp_sdr_unregister()` releases the registration; live instances must be
destroyed first. `rrp_sdr_find()` returns a borrowed driver descriptor.
The driver descriptor and callbacks must remain valid while registered.
Configuration loading for bridge devices is implemented in `cfg.bridge.c`;
the public API remains declared in `bridge.config.h`.

## Rooms and network transports (`rrprotocol.h`, `ws.h`, `rtp.h`)

Room validation and membership checks are predicates. Send/command helpers
are operations; a successful local send is not necessarily a remote
acknowledgment. Connection pointers and event-loop objects are not
caller-owned. Transport-specific threading or callback dispatch must be
verified by the application; there is no blanket thread-safety guarantee.

## Configuration and dependencies

`config_fwdsp_init()`, `cfg_servers_init()`, and
`rrp_bridge_config_init()` register subsystem config handling.
The related implementations are in `cfg.fwdsp.c`, `cfg.servers.c` and
`cfg.bridge.c` respectively. Use matching librustyaxe/librrprotocol
revisions when building the main RustyRig application.

## API audit notes

Prior to declaring ABI stability, verify callback lifetime and ownership
contracts, audit exported `bool` operations for inverted legacy returns, and
test failure paths as well as successes. Avoid converting native dictionary
types to strings merely to move them between in-process components.
