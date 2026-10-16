#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
for mode in enabled disabled; do
   extra=()
   if [ "$mode" = disabled ]; then extra=(-DTEST_NO_PROFILE); fi
   ${CC:-cc} -I. -Iinc -Ibuild/${PROFILE:-radio} "${extra[@]}" \
      librrprotocol/tests/wire_profile.c -L. -Wl,-rpath,"$PWD" \
      -lrrprotocol -lrustyaxe -lm -pthread -o "$work/profile-$mode"
   "$work/profile-$mode"
done
