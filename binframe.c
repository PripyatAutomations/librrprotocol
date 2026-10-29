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
// PARITY: rustyrig-www/js/webui.binframe.js:binframe_parse
//
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.binframe.h>

const uint8_t rr_binframe_magic[2] = {
   'R', 'R'
};

int rr_binframe_pack_hdr(uint8_t *out, size_t outlen, uint8_t subsystem, const char codec[4], uint8_t direction, uint8_t vfo, uint8_t rig, uint8_t stream,
   uint32_t seq, uint32_t payload_len, uint64_t ts) {
   if (!out || outlen < RR_BINFRAME_HDR_LEN || !codec) {
      return -1;
   }

   if (payload_len > RR_BINFRAME_MAX_PAYLOAD) {
      Log(LOG_DEBUG, "binframe", "pack_hdr: payload_len %u exceeds limit %d", payload_len, RR_BINFRAME_MAX_PAYLOAD);

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

int rr_binframe_parse(const uint8_t *buf, size_t len, struct rr_binframe *f) {
   if (!buf || !f || len < RR_BINFRAME_HDR_LEN) {
      return -1;
   }
   memset(f, 0, sizeof(*f));

   if (buf[0] != rr_binframe_magic[0] || buf[1] != rr_binframe_magic[1]) {
      Log(LOG_DEBUG, "binframe", "Dropping frame with bad magic %02X%02X", buf[0], buf[1]);

      return -1;
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

   if (payload_len > RR_BINFRAME_MAX_PAYLOAD || RR_BINFRAME_HDR_LEN + (size_t)payload_len != len) {
      Log(LOG_DEBUG, "binframe", "Dropping frame: payload_len %u invalid for %zu byte frame", payload_len, len);

      return -1;
   }

   uint64_t ts;
   memcpy(&ts, buf + 20, sizeof(ts));
   ts = be64toh(ts);

   memset(&f->hdr, 0, sizeof(f->hdr));
   memcpy(f->hdr.magic, buf, sizeof(f->hdr.magic));
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

   Log(LOG_CRAZY, "ws.binframe",
      "Binary frame: %zu bytes, magic: %02X%02X version: %u subsystem: %02X codec: %.*s direction: %u vfo: %u rig: %u stream: %u seq: %u payload_len: %u ts: %"
      PRIu64, len, f->hdr.magic[0], f->hdr.magic[1], f->hdr.version, f->hdr.subsystem, (int)sizeof(f->hdr.codec), f->hdr.codec, f->hdr.direction, f->hdr.vfo, f
      ->hdr.rig, f->hdr.stream, f->hdr.seq, f->hdr.payload_len, f->hdr.ts);

   return 0;
}

int rr_binframe_frame(uint8_t **out, uint8_t subsystem, const char codec[4], uint8_t direction, uint8_t vfo, uint8_t rig, uint8_t stream, uint32_t seq, uint64_t
   ts, const void *payload, size_t payload_len) {
   if (!out || !payload || payload_len > RR_BINFRAME_MAX_PAYLOAD) {
      return -1;
   }

   uint8_t *buf = malloc(RR_BINFRAME_HDR_LEN + payload_len);

   if (!buf) {
      Log(LOG_CRIT, "binframe", "OOM packing frame");

      return -1;
   }

   int hlen = rr_binframe_pack_hdr(buf, RR_BINFRAME_HDR_LEN, subsystem, codec, direction, vfo, rig, stream, seq, (uint32_t) payload_len, ts);

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
/* Chunked Host Log payloads use codec "lgch": normal 17-byte log header, followed by network-order (message ID, total bytes, offset), then bytes. Reassembly is
 * per connection, bounded and rejects gaps/overlaps. */
#define RR_LOG_CHUNK_MAX (64u * 1024u * 1024u)
typedef struct rr_log_chunk_pending {
   void *connection;
   uint32_t id, total, received;
   uint64_t last_seen;
   uint8_t prefix[RR_LOGFRAME_HDR_LEN];
   uint8_t *text;
   struct rr_log_chunk_pending *next;
} rr_log_chunk_pending_t;
static rr_log_chunk_pending_t *log_pending;
static uint32_t rr_log_chunk_u32(const uint8_t *p) {
   return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}
static void rr_log_chunk_remove(rr_log_chunk_pending_t **slot) {
   rr_log_chunk_pending_t *entry = *slot;
   *slot = entry->next;
   free(entry->text);
   free(entry);
}
static bool rr_log_chunk_dispatch(const struct rr_binframe *f, void *ctx) {
   if (f->len < RR_LOGFRAME_HDR_LEN + 12) {
      return false;
   }
   const uint8_t *p = f->data + RR_LOGFRAME_HDR_LEN;
   uint32_t id = rr_log_chunk_u32(p), total = rr_log_chunk_u32(p + 4);
   uint32_t offset = rr_log_chunk_u32(p + 8);
   size_t n = f->len - RR_LOGFRAME_HDR_LEN - 12;
   rr_log_chunk_pending_t **slot = &log_pending;
   /* Purge stale or interrupted transfers. The sender sends in order. */
   uint64_t timestamp = (uint64_t) time(NULL);
   while (*slot) {
      rr_log_chunk_pending_t *e = *slot;

      if ((timestamp > e->last_seen && timestamp - e->last_seen > 60) || (e->connection == ctx && (e->id != id || (offset == 0 && e->received)))) {
         rr_log_chunk_remove(slot);
      } else {
         slot = &e->next;
      }
   }

   if (!total || total > RR_LOG_CHUNK_MAX || offset > total || n > total - offset) {
      return false;
   }
   slot = &log_pending;
   while (*slot && !((*slot)->connection == ctx && (*slot)->id == id)) {
      slot = &(*slot)->next;
   }

   if (!*slot) {
      if (offset != 0) {
         return false;
      }
      rr_log_chunk_pending_t *e = calloc(1, sizeof(*e));

      if (!e) {
         return false;
      }
      e->text = malloc((size_t)total + RR_LOGFRAME_HDR_LEN + 1);

      if (!e->text) {
         free(e);

         return false;
      }
      e->connection = ctx;
      e->id = id;
      e->total = total;
      memcpy(e->prefix, f->data, RR_LOGFRAME_HDR_LEN);
      e->next = log_pending;
      log_pending = e;
      slot = &log_pending;
   }
   rr_log_chunk_pending_t *e = *slot;

   if (offset != e->received || memcmp(e->prefix, f->data, RR_LOGFRAME_HDR_LEN)) {
      rr_log_chunk_remove(slot);

      return false;
   }
   memcpy(e->text + RR_LOGFRAME_HDR_LEN + offset, p + 12, n);
   e->received += (uint32_t) n;
   e->last_seen = timestamp;

   if (e->received == e->total) {
      memcpy(e->text, e->prefix, RR_LOGFRAME_HDR_LEN);
      e->text[RR_LOGFRAME_HDR_LEN + e->total] = 0;
      event_emit_binary("media.frame.log", (rrconn_t *) ctx, e->text, RR_LOGFRAME_HDR_LEN + (size_t)e->total + 1);
      rr_log_chunk_remove(slot);
   }

   return true;
}

bool rr_binframe_dispatch(struct rr_binframe *f, void *ctx) {
   if (!f) {
      return false;
   }
   const char *evname = NULL;

   switch (f->hdr.subsystem) {
      case RR_BINFRAME_SUBSYS_AUDIO: {
         evname = "media.frame.audio";
         break;
      }
      case RR_BINFRAME_SUBSYS_VIDEO: {
         evname = "media.frame.video";
         break;
      }
      case RR_BINFRAME_SUBSYS_WATERFALL: {
         evname = "media.frame.waterfall";
         break;
      }
      case RR_BINFRAME_SUBSYS_MODEM: {
         evname = "media.frame.modem";
         break;
      }
      case RR_BINFRAME_SUBSYS_FILE: {
         evname = "media.frame.file";
         break;
      }
      case RR_BINFRAME_SUBSYS_CONTROL: {
         evname = "media.frame.control";
         break;
      }
      case RR_BINFRAME_SUBSYS_LOG: {
         if (!memcmp(f->hdr.codec, "lgch", 4)) {
            return rr_log_chunk_dispatch(f, ctx);
         }
         evname = "media.frame.log";
         break;
      }
      case RR_BINFRAME_SUBSYS_KEEPALIVE: {
         Log(LOG_DEBUG, "binframe", "keepalive frame seq=%u", f->hdr.seq);

         return true;
      }
      default: {
         Log(LOG_DEBUG, "binframe", "Dropping frame with unknown subsystem 0x%02X", f->hdr.subsystem);

         return false;
      }
   }
   event_emit_binary(evname, (rrconn_t *) ctx, f->data, f->len);

   return true;
}

// Pack a host log line into a SUBSYS_LOG binframe. The payload is a
// fixed RR_LOGFRAME_HDR_LEN header (prio byte + NUL-padded subsys)
// followed by the NUL-terminated log message, unmangled. Called from
// the server's log callback (rrserver/hostlog.c); consumers parse it
// back in the client (PARITY: rrclient/gtk.syslog.c host_log_frame_handler).
int rr_logframe_frame(uint8_t **out, logpriority_t priority, const char *subsys, const char *msg, size_t msg_len, uint32_t seq, uint64_t ts) {
   if (!out || !subsys || !msg) {
      return -1;
   }
   size_t plen = RR_LOGFRAME_HDR_LEN + msg_len + 1;

   if (plen > RR_BINFRAME_MAX_PAYLOAD) {
      /* The v2 WebSocket message limit is a transport bound, not a logger buffer limit. Never silently present a truncated report as complete. */
      static const char notice[] = "\n[Host Log transport limit reached: remainder omitted]";
      const size_t available = RR_BINFRAME_MAX_PAYLOAD - RR_LOGFRAME_HDR_LEN - 1;
      const size_t notice_len = sizeof(notice) - 1;
      msg_len = available > notice_len ? available - notice_len : 0;
      plen = RR_BINFRAME_MAX_PAYLOAD;
   }
   uint8_t *payload = malloc(plen);

   if (!payload) {
      Log(LOG_CRIT, "binframe", "OOM packing logframe");

      return -1;
   }
   memset(payload, 0, plen);
   payload[0] = (uint8_t) priority;
   snprintf((char *)payload + 1, sizeof(struct rr_logframe) - 1, "%s", subsys);
   memcpy(payload + RR_LOGFRAME_HDR_LEN, msg, msg_len);

   if (plen > RR_LOGFRAME_HDR_LEN + msg_len + 1) {
      static const char notice[] = "\n[Host Log transport limit reached: remainder omitted]";
      memcpy(payload + RR_LOGFRAME_HDR_LEN + msg_len, notice, sizeof(notice) - 1);
   }
   // payload was zero-initialized, including the final NUL

   int flen = rr_binframe_frame(out, RR_BINFRAME_SUBSYS_LOG, "text", RR_BINFRAME_DIR_RX, RR_BINFRAME_VFO_NA, RR_BINFRAME_RIG_NA, RR_BINFRAME_STREAM_NONE, seq,
      ts, payload, plen);
   free(payload);

   return flen;
}
