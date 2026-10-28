#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} ${CFLAGS:-} -std=c11 -Wall -Wextra -Werror -I. \
   librrprotocol/tests/transport_lifecycle.c librrprotocol/transport.c \
   ${LDFLAGS:-} -o "$work/transport_lifecycle"
"$work/transport_lifecycle"
