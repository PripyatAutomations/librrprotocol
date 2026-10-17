#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -I. -Ibuild/${PROFILE:-radio} \
   librrprotocol/tests/rtp_socket_filter.c -L. -Wl,-rpath,"$PWD" -lrrprotocol -lrustyaxe -o "$work/rtp-filter"
"$work/rtp-filter"
