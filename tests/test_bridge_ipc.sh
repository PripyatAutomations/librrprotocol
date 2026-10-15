#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} ${CFLAGS:-} -std=gnu11 -I. librrprotocol/tests/bridge_ipc.c \
   librrprotocol/bridge.ipc.c ${LDFLAGS:-} -o "$work/bridge-ipc"
"$work/bridge-ipc"
