#!/usr/bin/env python3
"""Compare documented current JSON with a proposed readable envelope.

This is an audit tool, not a protocol encoder. It changes no wire behavior.
Sizes exclude WebSocket/TLS/TCP overhead and assume compact UTF-8 JSON.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def size(value):
    return len(json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))


def proposed(message):
    family = message["msg"]["type"]
    body = dict(message[family])
    command = body.pop("cmd")
    result = {"op": f"{family}.{command}", **body}
    for key in ("target", "request", "stream"):
        if key in message:
            result[key] = message[key]
    if "ts" in message["msg"]:
        result["ts"] = message["msg"]["ts"]
    return result


def compare(label, old, new):
    a = sum(size(item) for item in old) if isinstance(old, list) else size(old)
    b = size(new)
    print(f"| {label} | {a} | {b} | {(a-b)*100/a:.1f}% |")


def main():
    print("| Sample | Current bytes | Proposed bytes | Reduction |")
    print("| --- | ---: | ---: | ---: |")
    examples = []
    for line in (ROOT / "doc/object-property-protocol.md").read_text().splitlines():
        if line.startswith('{"msg":'):
            examples.append(json.loads(line))
    for family, command in (("object", "snapshot"), ("object", "descriptor"),
                            ("property", "descriptor"), ("property", "changed")):
        message = next(m for m in examples if m["msg"]["type"] == family
                       and m[family]["cmd"] == command)
        compare(f"{family}.{command}", message, proposed(message))
    message = next(m for m in examples if m["msg"]["type"] == "property"
                   and m["property"]["cmd"] == "changed")
    compact = proposed(message)
    # Only valid after explicit link-local bindings and epoch negotiation.
    bound = dict(compact)
    bound["target"] = 5
    bound["stream"] = {"seq": message["stream"]["seq"]}
    compare("property.changed (bound target/epoch)", message, bound)
    for count in (4, 16):
        batch = {"op": "property.batch", "stream": message["stream"], "items": []}
        for index in range(count):
            item = dict(compact)
            item.pop("op")
            item.pop("stream")
            item["target"] = f"00000000-0000-4000-8000-{index+1:012d}"
            batch["items"].append(item)
        compare(f"{count} property changes (same stream cursor)",
                [message] * count, batch)
    cat = {"msg": {"type": "cat", "ts": 1791580800},
           "cat": {"cmd": "freq", "room": "#rig1", "freq": 145000000,
                   "state": {"freq": 145000000}, "user": "alice", "vfo": "A"}}
    clean = proposed(cat)
    clean.pop("state")
    compare("cat.freq (single authoritative value)", cat, clean)
    print("\nAt 50 audio frames/s, the existing 28-byte media header costs 1,400 bytes/s.")
    print("A hypothetical 16-byte negotiated header would cost 800 bytes/s (600 saved).")
    print("The latter requires stream bindings and is not an implemented format.")


if __name__ == "__main__":
    main()
