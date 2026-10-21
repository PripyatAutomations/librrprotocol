# RustyRig wire protocol v2 (pre-release)

## Compatibility boundaries

- Client WebSocket subprotocol: `rustyrig.v2`.
- Server-to-server WebSocket subprotocol: `rustyrig-link.v2`.
- Binary media/control frame: existing `RR` magic and 28-byte header, header byte 2 is now `0x02`. All current audio, video, waterfall, modem, file, control, host-log and keepalive frames use this header and reject other versions before dispatch.
- Local bridge `SOCK_SEQPACKET` IPC: independently versioned as `RRP_BRIDGE_IPC_VERSION=4`, with an explicit version check. Do **not** tie this local ABI to the network major version.

A v1 peer is incompatible and must reconnect to a v2 endpoint after upgrade. The WebSocket upgrade gate rejects a mismatched client subprotocol with HTTP 426. Server links similarly use their distinct v2 subprotocol.

## CAP handshake

Once the WebSocket upgrades successfully, the client sends:

```json
{"op":"cap.req","version":2,"features":"wire.json-v2 binframe.v2 object.inventory-v2"}
```

The server returns `cap.ack` with accepted features or `cap.nak` with an error reason. The client waits for `cap.ack` with its mandatory JSON and binary capabilities before sending HELLO/login.

`cap.ls` requests the advertised feature set. Tokens are space-separated, ASCII feature identifiers. Tokens not advertised by the server are rejected rather than presumed available.

This is the **initial capability baseline**, not yet a mechanism for changing the binary layout within v2. Future optional features must be gated by the negotiated feature set on every sender and receiver path; merely advertising a feature cannot activate it.

## Efficiency and evolution

The compact JSON operation registry already maps logical commands to single `op` names and short field names. v2 retains that wire layout while adding capability negotiation and stricter major-version boundaries. The binary header is unchanged in size.

Optimize envelope size and repeating inventory metadata only after collecting per-command byte counts and profiling the browser/native decoders; new encodings (CBOR, string tables, schema IDs, etc.) should be independently negotiated capabilities and must include test vectors for cross-language parity.

**Next work before release:** persistent per-connection negotiated capabilities, enforcing capability gates for optional operations and peer link features, browser parity/regeneration, negative tests for v1/v2 handshakes, and connection teardown on CAP timeout.

## Deferred WebUI v2 migration checklist

**Status: intentionally deferred while native GTK/TUI client and rrserver v2 are stabilized.**
Do not interpret WebUI failure to connect to v2 rrserver as a CAT, GTK or server-link regression. Do not downgrade the v2 handshake to make the old browser work.

- [ ] Request `rustyrig.v2` explicitly in `new WebSocket(url, "rustyrig.v2")`; handle rejected upgrade with a useful incompatible-version message instead of an endless reconnect loop.
- [ ] Adopt the compact v2 `op` wire envelope, fields and strict registry mappings from `librrprotocol/wire-schema.json` (generated C: `librrprotocol/wire-registry.h`; generated browser artifact target: `www/js/webui.wire.registry.js`). Replace old nested `msg.type` payloads at every send/receive callsite; do not guess mappings.
- [ ] Implement `cap.ls` / `cap.req` / `cap.ack` / `cap.nak` / `cap.end` handling. Request `wire.json-v2 binframe.v2 object.inventory-v2` and only HELLO/login after required capabilities are acknowledged. Handle unsupported features and negotiation timeout.
- [ ] Parse/emit binary frames with version byte `0x02` in the existing 28-byte header. Update `js/webui.binframe.js` and tests for audio TX/RX, video, waterfall, serial/modem, file transfers, control and logging where supported; reject other versions.
- [ ] Bring browser object/property messages, rig controls, Explorer inventory, permission failures, media subscriptions, serial, file transfers, authentication, chat and latency/ping paths into parity with C clients.
- [ ] Add browser test vectors for compact JSON CAP messages and all other v2 operations, using the same authoritative schema as native tests. Include invalid/unknown fields, rejected v1 connections, partial/unknown CAP requests and binary version mismatch tests.
- [ ] Keep capability-dependent behavior off until negotiated; do not assume all v2 minor features are present. Mirror per-connection feature state when native implementation is finalized.
- [ ] Run a browser live test against a v2 rrserver (login, basic chat, Explorer/rig control, audio and reconnect) before calling the WebUI supported.

**Likely files:** `rustyrig-www/js/webui.js`, `rustyrig-www/js/webui.binframe.js`, `rustyrig-www/js/webui.auth.js`, `rustyrig-www/js/webui.media.js`, and the other `webui.*.js` producers/consumers. The schema generator in `librrprotocol/tests/generate_wire.py` targets a browser registry under `www/js`; verify its output path against the actual `rustyrig-www` checkout when implementing.

## Native v2 stabilization gate (current priority)

- [ ] Compile `librrprotocol`, rrserver and GTK rrclient together after each schema change.
- [ ] Test CAP before authentication, ACK/NAK, unknown feature requests, client timeout, reconnect, and mandatory-feature rejection.
- [ ] Confirm the server refuses incompatible major subprotocols; validate server-link `rustyrig-link.v2` across two upgraded servers.
- [ ] Confirm binary version `0x02` on both directions for audio, serial, media and logging, and reject version `0x01` cleanly.
- [ ] Test GTK Explorer inventory, Yaesu rig controls and room ownership with real linked servers.
- [ ] Implement persistent negotiated per-connection capabilities and gate optional operations on them before release.
