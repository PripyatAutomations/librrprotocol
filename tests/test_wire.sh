#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tests/generate_wire.py --check
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
: "${CC:=cc}"
"$CC" ${CFLAGS:-} -std=gnu2x -Ibuild/include -Ibuild/generated \
   $(pkg-config --cflags librustyaxe glib-2.0 gio-2.0 openssl) \
   tests/wire.c -Lbuild -Wl,-rpath,"$PWD/build" \
   -lrrprotocol $(pkg-config --libs librustyaxe glib-2.0 gio-2.0 openssl) \
   -largon2 -lm -pthread ${LDFLAGS:-} -o "$work/wire"
LD_LIBRARY_PATH="$PWD/build${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/wire"
