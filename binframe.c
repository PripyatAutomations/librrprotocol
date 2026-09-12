//
// librrprotocol/binframe.c: Websocket binary media frame (de)serialization
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Implements the binframe v2 wire format specified in doc/media-frames.md
//
// PARITY: rustyrig-www/js/webui.audio.framing.js
//
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.binframe.h>

const uint8_t rr_binframe_magic[2] = { 'R', 'R' };

int rr_binframe_pack_hdr(uint8_t *out, size_t outlen, uint8_t subsystem,
   const char codec[4], uint8_t direction, uint8_t vfo, uint8_t rig,
   uint8_t stream, uint32_t seq, uint32_t payload_len, uint64_t ts) {
   if (!out || outlen < RR_BINFRAME_HDR_LEN || !codec) {
      return -1;
   }
   if (payload_len > RR_BINFRAME_MAX_PAYLOAD) {
      Log(LOG_DEBUG, "binframe", "pack_hdr: payload_len %u exceeds limit %d",
         payload_len, RR_BINFRAME_MAX_PAYLOAD);
      return -1;
   }

   memset(out, 0, RR_BINFRAME_HDR_LEN);
   out[0] = rr_binframe_magic[0];
   out[1] = rr_binframe_magic[1];
   out[2] = RR_BINFRAME_VERSION;
   out[3] = subsystem;
   memcpy(out + 4, codec, 4);
   out[8] = direction;
   out[9] = vfo;
   out[10] = rig;
   out[11] = stream;

   uint32_t nseq = htonl(seq);
   uint32_t nlen = htonl(payload_len);
   memcpy(out + 12, &nseq, sizeof(nseq));
   memcpy(out + 16, &nlen, sizeof(nlen));

   uint64_t nts = htobe64(ts);
   memcpy(out + 20, &nts, sizeof(nts));

   return RR_BINFRAME_HDR_LEN;
}

// Is this one of the legacy formats that predate the v2 header?
// Returns: 0 = not legacy, 1 = legacy 4-byte audio framing,
// 2 = legacy 24-byte file-xfer framing, -1 = unrecognized garbage
static int legacy_frame_check(const uint8_t *buf, size_t len) {
   if (len < 4) {
      return -1;
   }
   // ws.file-xfer.c chunk: 8-byte id then 4-byte chunk index; the top
   // two bytes of an id are effectively random, not 'RR'
   if (len >= 24) {
      // Heuristic: chunk index is sane (< 2^24 chunks) and id is nonzero
      uint32_t idx;
      memcpy(&idx, buf + 8, sizeof(idx));
      if (idx < 0x01000000) {
         return 2;
      }
   }
   // old audio framing: uint16 chan then uint16 seq (big-endian)
   uint16_t chan, seq;
   memcpy(&chan, buf, sizeof(chan));
   memcpy(&seq, buf + 2, sizeof(seq));
   chan = ntohs(chan);
   seq = ntohs(seq);

   if (chan != rr_binframe_magic[0] * 256 + rr_binframe_magic[1]) {
      return 1;
   }
   return -1;
}

int rr_binframe_parse(const uint8_t *buf, size_t len, struct rr_binframe *f) {
   if (!buf || !f || len < RR_BINFRAME_HDR_LEN) {
      return -1;
   }
   memset(f, 0, sizeof(*f));

   if (buf[0] != rr_binframe_magic[0] || buf[1] != rr_binframe_magic[1]) {
      return legacy_frame_check(buf, len);
   }
   if (buf[2] != RR_BINFRAME_VERSION) {
      Log(LOG_DEBUG, "binframe", "Dropping frame with unknown version %u", buf[2]);
      return -1;
   }

   uint32_t seq, payload_len;
   memcpy(&seq, buf + 12, sizeof(seq));
   memcpy(&payload_len, buf + 16, sizeof(payload_len));
   seq = ntohl(seq);
   payload_len = ntohl(payload_len);

   if (payload_len > RR_BINFRAME_MAX_PAYLOAD || RR_BINFRAME_HDR_LEN + (size_t)payload_len > len) {
      Log(LOG_DEBUG, "binframe", "Dropping frame: payload_len %u invalid for %zu byte frame",
         payload_len, len);
      return -1;
   }

   uint64_t ts;
   memcpy(&ts, buf + 20, sizeof(ts));
   ts = be64toh(ts);

   memset(&f->hdr, 0, sizeof(f->hdr));
   f->hdr.version = buf[2];
   f->hdr.subsystem = buf[3];
   memcpy(f->hdr.codec, buf + 4, 4);
   f->hdr.direction = buf[8];
   f->hdr.vfo = buf[9];
   f->hdr.rig = buf[10];
   f->hdr.stream = buf[11];
   f->hdr.seq = seq;
   f->hdr.payload_len = payload_len;
   f->hdr.ts = ts;
   f->data = buf + RR_BINFRAME_HDR_LEN;
   f->len = payload_len;

   return 0;
}

int rr_binframe_frame(uint8_t **out, uint8_t subsystem, const char codec[4],
   uint8_t direction, uint8_t vfo, uint8_t rig, uint8_t stream, uint32_t seq,
   uint64_t ts, const void *payload, size_t payload_len) {
   if (!out || !payload || payload_len > RR_BINFRAME_MAX_PAYLOAD) {
      return -1;
   }
   uint8_t *buf = malloc(RR_BINFRAME_HDR_LEN + payload_len);

   if (!buf) {
      Log(LOG_CRIT, "binframe", "OOM packing frame");
      return -1;
   }
   int hlen = rr_binframe_pack_hdr(buf, RR_BINFRAME_HDR_LEN, subsystem, codec,
      direction, vfo, rig, stream, seq, (uint32_t)payload_len, ts);

   if (hlen < 0) {
      free(buf);
      return -1;
   }
   memcpy(buf + RR_BINFRAME_HDR_LEN, payload, payload_len);
   *out = buf;

   return (int)(RR_BINFRAME_HDR_LEN + payload_len);
}

// Default subsystem handlers: emit a binary event for the program/UI
// layer. The event data is the raw payload of the frame; recipients
// must not free it or retain the pointer beyond the callback. Use
// event_on_binary("media.frame.<subsystem>", cb, user) to subscribe.
bool rr_binframe_dispatch(struct rr_binframe *f, void *ctx) {
   if (!f) {
      return true;
   }
   const char *evname = NULL;

   switch (f->hdr.subsystem) {
      case RR_BINFRAME_SUBSYS_AUDIO:
         evname = "media.frame.audio";
         break;
      case RR_BINFRAME_SUBSYS_VIDEO:
         evname = "media.frame.video";
         break;
      case RR_BINFRAME_SUBSYS_WATERFALL:
         evname = "media.frame.waterfall";
         break;
      case RR_BINFRAME_SUBSYS_MODEM:
         evname = "media.frame.modem";
         break;
      case RR_BINFRAME_SUBSYS_FILE:
         evname = "media.frame.file";
         break;
      case RR_BINFRAME_SUBSYS_CONTROL:
         evname = "media.frame.control";
         break;
      case RR_BINFRAME_SUBSYS_LOG:
         evname = "media.frame.log";
         break;
      case RR_BINFRAME_SUBSYS_KEEPALIVE:
         Log(LOG_DEBUG, "binframe", "keepalive frame seq=%u", f->hdr.seq);
         return false;
      default:
         Log(LOG_DEBUG, "binframe", "Dropping frame with unknown subsystem 0x%02X",
            f->hdr.subsystem);
         return true;
   }
   event_emit_binary(evname, (rrconn_t *)ctx, f->data, f->len);
   return false;
}

// Pack a host log line into a SUBSYS_LOG binframe. The payload is a
// fixed RR_LOGFRAME_HDR_LEN header (prio byte + NUL-padded subsys)
// followed by the NUL-terminated log message, unmangled. Called from
// the server's log callback (rrserver/hostlog.c); consumers parse it
// back in the client (PARITY: rrclient/gtk.syslog.c host_log_frame_handler).
int rr_logframe_frame(uint8_t **out, logpriority_t priority,
   const char *subsys, const char *msg, size_t msg_len,
   uint32_t seq, uint64_t ts) {
   if (!out || !subsys || !msg) {
      return -1;
   }
   size_t plen = RR_LOGFRAME_HDR_LEN + msg_len + 1;

   if (plen > RR_BINFRAME_MAX_PAYLOAD) {
      // Truncate over-long log lines rather than dropping them; a log
      // viewer losing a tail is better than losing the line entirely.
      plen = RR_BINFRAME_MAX_PAYLOAD;
      msg_len = plen - RR_LOGFRAME_HDR_LEN - 1;
   }
   uint8_t *payload = malloc(plen);

   if (!payload) {
      Log(LOG_CRIT, "binframe", "OOM packing logframe");
      return -1;
   }
   memset(payload, 0, plen);
   payload[0] = (uint8_t)priority;
   snprintf((char *)payload + 1, sizeof(struct rr_logframe) - 1, "%s", subsys);
   memcpy(payload + RR_LOGFRAME_HDR_LEN, msg, msg_len);
   // payload[RR_LOGFRAME_HDR_LEN + msg_len] is already NUL

   int flen = rr_binframe_frame(out, RR_BINFRAME_SUBSYS_LOG, "text",
      RR_BINFRAME_DIR_RX, RR_BINFRAME_VFO_NA, RR_BINFRAME_RIG_NA,
      RR_BINFRAME_STREAM_NONE, seq, ts, payload, plen);
   free(payload);

   return flen;
}
