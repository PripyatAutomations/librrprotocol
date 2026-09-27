#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections \
   -c librrprotocol/srv.http.c -o "$work/srv.http.o"
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   -ffunction-sections -fdata-sections \
   librrprotocol/tests/ptt_disconnect.c "$work/srv.http.o" \
   -Wl,--gc-sections -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe ${LDFLAGS:-} -o "$work/ptt_disconnect"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
   "$work/ptt_disconnect"
