#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -Wall -Wextra -Werror -I. -Iinc -Ibuild/${PROFILE:-radio} \
   librrprotocol/tests/irc_live.c -L. -Wl,-rpath,"$PWD" \
   -lrrprotocol -lrustyaxe ${LDFLAGS:-} -o "$work/irc_live"
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$work/key.pem" \
   -out "$work/cert.pem" -days 1 -subj /CN=localhost >/dev/null 2>&1
LD_LIBRARY_PATH="$PWD${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
   python3 librrprotocol/tests/irc_live.py "$work/irc_live" "$work/cert.pem" "$work/key.pem"
