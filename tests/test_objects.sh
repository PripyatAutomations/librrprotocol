#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} ${CFLAGS:-} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   librrprotocol/tests/objects.c -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe ${LDFLAGS:-} -o "$work/objects"
"$work/objects"
