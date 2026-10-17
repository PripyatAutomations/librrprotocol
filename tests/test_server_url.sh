#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -Wall -Wextra -Werror -I. librrprotocol/tests/server_url.c \
   librrprotocol/server.url.c ${LDFLAGS:-} -o "$work/server_url"
"$work/server_url"
