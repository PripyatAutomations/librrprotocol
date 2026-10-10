#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} \
   librrprotocol/tests/irc_config.c -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe ${LDFLAGS:-} -o "$work/irc_config"
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$work/irc_config"
