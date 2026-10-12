#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -I. -Iinc -Ibuild/${PROFILE:-radio} \
   librrprotocol/tests/dcc.c -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe -o "$work/dcc"
"$work/dcc"
