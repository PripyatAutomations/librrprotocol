#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections librrprotocol/tests/auth_security.c \
   librrprotocol/srv.auth.c -Wl,--gc-sections \
   -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe \
   -o "$work/auth_security"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/auth_security"
