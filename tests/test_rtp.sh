#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
if ! pkg-config --exists libsrtp2 && [[ -z ${SRTP_CFLAGS:-} ]]; then
   echo 'SKIP: RTP tests require libsrtp2 development headers'
   exit 0
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -DRR_HAVE_SRTP=1 -I. \
   ${SRTP_CFLAGS:-$(pkg-config --cflags libsrtp2)} librrprotocol/rtp.c librrprotocol/tests/rtp.c \
   ${SRTP_LIBS:-$(pkg-config --libs libsrtp2)} -o "$work/rtp"
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -DRR_HAVE_SRTP=1 -DRR_RTP_ENCRYPTION=1 -I. \
   ${SRTP_CFLAGS:-$(pkg-config --cflags libsrtp2)} librrprotocol/rtp.c librrprotocol/tests/rtp.c \
   ${SRTP_LIBS:-$(pkg-config --libs libsrtp2)} -o "$work/rtp-encrypted"
"$work/rtp-encrypted" 1
"$work/rtp"
