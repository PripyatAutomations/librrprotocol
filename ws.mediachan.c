//
// librrprotocol/ws.mediachan.c: media channel registry, subscribe protocol
//    This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
//
// Server side of the media channel protocol:
//
//  - The program (rrserver) creates channels for each streamable media
//    direction (e.g. audio RX on VFO A, audio TX for the rig) with
//    media_chan_add(); each gets a UUID.
//  - Clients learn about channels via `media.available` messages (sent
//    after auth and on demand with `media.cmd: list`).
//  - Clients subscribe with `media.cmd: subscribe` + `media.chan-uuid`;
//    the server records the channel in the connection's rx_channels /
//    tx_channels table and confirms with `media.cmd: subscribed`.
//
// PARITY: rustyrig-www/js/webui.media.js
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>

extern time_t now;

#ifndef MAX_MEDIA_CHANNELS
#define	MAX_MEDIA_CHANNELS 64
#endif

// The registry; the program (rrserver) fills this in via media_chan_add()
struct rr_mediachan media_channels[MAX_MEDIA_CHANNELS];

// Fill in a uuid for a new channel: use a random-ish but stable string
static void media_gen_uuid(char *out, size_t len) {
   static uint64_t counter = 0;

   snprintf(out, len, "%llx-%04llx", (unsigned long long)now,
      (unsigned long long)((uintptr_t)&media_channels[counter] + ++counter));
}

struct rr_mediachan *media_chan_add(uint8_t subsystem, uint8_t direction,
   uint8_t vfo, uint8_t rig, const char *codec, const char *descr) {
   // Already have this routing quadruple?
   struct rr_mediachan *cp = media_chan_find(subsystem, direction, vfo, rig);

   if (cp) {
      return cp;
   }
   for (int i = 0 ; i < MAX_MEDIA_CHANNELS ; i++) {
      if (media_channels[i].uuid[0] != '\0') {
         continue;
      }
      cp = &media_channels[i];
      memset(cp, 0, sizeof(*cp));
      media_gen_uuid(cp->uuid, sizeof(cp->uuid));
      cp->subsystem = subsystem;
      cp->direction = direction;
      cp->vfo = vfo;
      cp->rig = rig;

      if (codec) {
         snprintf(cp->codec, sizeof(cp->codec), "%s", codec);
      }
      if (descr) {
         snprintf(cp->descr, sizeof(cp->descr), "%s", descr);
      }
      Log(LOG_DEBUG, "ws.media", "Added media channel %s: subsys 0x%02X dir %s vfo %u rig %u (%s)",
         cp->uuid, subsystem, (direction == RR_BINFRAME_DIR_TX ? "tx" : "rx"), vfo, rig,
         (descr ? descr : "-"));

      return cp;
   }
   Log(LOG_CRIT, "ws.media", "media_chan_add: channel table full!");

   return NULL;
}

struct rr_mediachan *media_chan_find(uint8_t subsystem, uint8_t direction,
   uint8_t vfo, uint8_t rig) {
   for (int i = 0 ; i < MAX_MEDIA_CHANNELS ; i++) {
      struct rr_mediachan *cp = &media_channels[i];

      if (cp->uuid[0] == '\0') {
         continue;
      }
      if (cp->subsystem == subsystem && cp->direction == direction &&
          cp->vfo == vfo && cp->rig == rig) {
         return cp;
      }
   }
   return NULL;
}

struct rr_mediachan *media_chan_find_uuid(const char *uuid) {
   if (!uuid || uuid[0] == '\0') {
      return NULL;
   }
   for (int i = 0 ; i < MAX_MEDIA_CHANNELS ; i++) {
      if (media_channels[i].uuid[0] != '\0' &&
          strcasecmp(media_channels[i].uuid, uuid) == 0) {
         return &media_channels[i];
      }
   }
   return NULL;
}

void media_channels_free(void) {
   memset(media_channels, 0, sizeof(media_channels) );
}

// Send one media.available message to a client
bool media_send_available(rrconn_t *cptr, struct rr_mediachan *cp) {
   if (!cptr || !cp || cp->uuid[0] == '\0') {
      return true;
   }
   dict *d = dict_new();
   dict_add(d, "msg.type", "media");
   dict_add(d, "media.cmd", "available");
   dict_add(d, "media.chan-uuid", cp->uuid);
   dict_add_ulong(d, "media.subsys", cp->subsystem);
   dict_add_ulong(d, "media.dir", cp->direction);
   dict_add_ulong(d, "media.vfo", cp->vfo);
   dict_add_ulong(d, "media.rig", cp->rig);
   dict_add_ulong(d, "media.ts", now);

   if (cp->codec[0] != '\0') {
      dict_add(d, "media.codec", cp->codec);
   }
   if (cp->descr[0] != '\0') {
      dict_add(d, "media.descr", cp->descr);
   }
   ws_send_dict(NULL, cptr, d, WEBSOCKET_OP_TEXT);
   dict_free(d);

   return false;
}

bool media_send_available_all(rrconn_t *cptr) {
   for (int i = 0 ; i < MAX_MEDIA_CHANNELS ; i++) {
      if (media_channels[i].uuid[0] != '\0') {
         media_send_available(cptr, &media_channels[i]);
      }
   }
   return false;
}

// Helper: is chan_id already in the user's channel array?
static bool chan_in_array(u_int32_t *arr, int max, u_int32_t chan_id) {
   for (int i = 0 ; i < max ; i++) {
      if (arr[i] == chan_id) {
         return true;
      }
   }
   return false;
}

// Helper: store chan_id in the first free slot of the user's channel array
static bool chan_add_to_array(u_int32_t *arr, int max, u_int32_t chan_id) {
   if (chan_in_array(arr, max, chan_id) ) {
      return false;   // already subscribed
   }
   for (int i = 0 ; i < max ; i++) {
      if (arr[i] == 0) {
         arr[i] = chan_id;

         return false;
      }
   }
   return true;
}

// Helper: remove chan_id from the user's channel array
static void chan_del_from_array(u_int32_t *arr, int max, u_int32_t chan_id) {
   for (int i = 0 ; i < max ; i++) {
      if (arr[i] == chan_id) {
         arr[i] = 0;

         return;
      }
   }
}

// Server-side handler for client media.* frames with media.cmd:
// `list`, `subscribe` and `unsubscribe`. Returns false if handled.
bool ws_handle_mediachan_msg(rrconn_t *cptr, dict *d) {
   if (!cptr || !d) {
      return true;
   }
   const char *media_cmd = dict_get(d, "media.cmd", NULL);
   const char *uuid = dict_get(d, "media.chan-uuid", NULL);

   if (!media_cmd) {
      Log(LOG_DEBUG, "ws.media", "media message without media.cmd");
      return true;
   }

   if (strcasecmp(media_cmd, "list") == 0) {
      // Client wants the (possibly updated) channel list
      media_send_available_all(cptr);

      return false;
   } else if (strcasecmp(media_cmd, "subscribe") == 0) {
      struct rr_mediachan *cp = media_chan_find_uuid(uuid);

      if (!cp) {
         Log(LOG_WARN, "ws.media", "Subscribe for unknown channel |%s| from %s",
            (uuid ? uuid : "<null>"), cptr->chatname);
         ws_send_error(cptr, "No such media channel");

         return true;
      }
      // Channel id is 1 + table index; 0 means "no channel" in the
      // rx_channels/tx_channels arrays
      u_int32_t chan_id = (u_int32_t)(cp - media_channels) + 1;
      bool is_tx = (cp->direction == RR_BINFRAME_DIR_TX);
      bool oom = (is_tx ?
         chan_add_to_array(cptr->tx_channels, MAX_TX_CHANNELS, chan_id) :
         chan_add_to_array(cptr->rx_channels, MAX_RX_CHANNELS, chan_id) );

      if (oom) {
         Log(LOG_WARN, "ws.media", "No free channel slots for %s subscribing to %s",
            cptr->chatname, cp->uuid);
         ws_send_error(cptr, "Too many media subscriptions");

         return true;
      }
      dict *sub = dict_new();
      dict_add(sub, "msg.type", "media");
      dict_add(sub, "media.cmd", "subscribed");
      dict_add(sub, "media.chan-uuid", cp->uuid);
      dict_add_ulong(sub, "media.stream", chan_id);
      dict_add_ulong(sub, "media.subsys", cp->subsystem);
      dict_add_ulong(sub, "media.dir", cp->direction);
      dict_add_ulong(sub, "media.vfo", cp->vfo);
      dict_add_ulong(sub, "media.rig", cp->rig);
      dict_add_ulong(sub, "media.ts", now);

      if (cp->codec[0] != '\0') {
         dict_add(sub, "media.codec", cp->codec);
      }
      ws_send_dict(NULL, cptr, sub, WEBSOCKET_OP_TEXT);
      dict_free(sub);
      Log(LOG_DEBUG, "ws.media", "Subscribed %s to channel %s (stream %u)", cptr->chatname, cp->uuid, chan_id);

      return false;
   } else if (strcasecmp(media_cmd, "unsubscribe") == 0) {
      struct rr_mediachan *cp = media_chan_find_uuid(uuid);

      if (!cp) {
         return true;
      }
      u_int32_t chan_id = (u_int32_t)(cp - media_channels) + 1;

      if (cp->direction == RR_BINFRAME_DIR_TX) {
         chan_del_from_array(cptr->tx_channels, MAX_TX_CHANNELS, chan_id);
      } else {
         chan_del_from_array(cptr->rx_channels, MAX_RX_CHANNELS, chan_id);
      }
      Log(LOG_DEBUG, "ws.media", "Unsubscribed %s from channel %s", cptr->chatname, cp->uuid);

      return false;
   }
   Log(LOG_DEBUG, "ws.media", "Unhandled media cmd: |%s|", media_cmd);

   return true;
}
