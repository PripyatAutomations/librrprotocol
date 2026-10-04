#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   librrprotocol/tests/media_codecs.c -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe -o "$work/media_codecs"
"$work/media_codecs"
