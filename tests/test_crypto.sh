#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
out=$(mktemp)
trap 'rm -f "$out"' EXIT HUP INT TERM
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -I. tests/crypto.c crypto.openssl.c crypto.cert.openssl.c -o "$out" $(pkg-config --cflags --libs openssl)
"$out"
