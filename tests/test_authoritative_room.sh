#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections librrprotocol/tests/test_authoritative_room.c \
   -Wl,--gc-sections -o "$work/test_authoritative_room"
"$work/test_authoritative_room"
