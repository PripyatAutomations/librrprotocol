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
