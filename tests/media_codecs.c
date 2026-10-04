// Rejected codec requests must not create channels or change their format.
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>

time_t now;
bool dying, restarting;

int main(void) {
   cfg = dict_new();
   dict_add(cfg, "codecs.allowed", "pc16 mu08 opuT ---- none");
   dict_add(cfg, "audio.test-mode", "true");
   rrconn_t client = { .authenticated = true };
   snprintf(client.media_codecs, sizeof(client.media_codecs), "pc16 opuT ---- none");
   dict *request = dict_new();
   dict_add(request, "media.cmd", "subscribe");
   const char *invalid[] = { "----", "none", "NONE", "", "pcm16", "xxxx", "mu08" };
   struct rr_mediachan snapshot[MAX_MEDIA_CHANNELS];
   memcpy(snapshot, media_channels, sizeof(snapshot));
   for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
      dict_add(request, "media.codec", invalid[i]);
      assert(!ws_handle_mediachan_msg(&client, request));
      assert(!memcmp(snapshot, media_channels, sizeof(snapshot)));
      assert(!client.rx_channels[0]);
   }
   dict_add(request, "media.codec", "opuT");
   dict_add_int(request, "media.dir", RR_BINFRAME_DIR_TX);
   assert(!ws_handle_mediachan_msg(&client, request));
   assert(!memcmp(snapshot, media_channels, sizeof(snapshot)));
   dict_add_int(request, "media.dir", RR_BINFRAME_DIR_RX);
   dict_add_int(request, "media.subsys", 257); // cannot bypass audio checks by truncation
   assert(!ws_handle_mediachan_msg(&client, request));
   dict_add_int(request, "media.subsys", RR_BINFRAME_SUBSYS_AUDIO);
   dict_add(cfg, "audio.test-mode", "false");
   assert(!ws_handle_mediachan_msg(&client, request));
   assert(!memcmp(snapshot, media_channels, sizeof(snapshot)));
   dict_add(request, "media.codec", "pc16");
   assert(ws_handle_mediachan_msg(&client, request));
   struct rr_mediachan *channel = media_chan_find(RR_BINFRAME_SUBSYS_AUDIO,
      RR_BINFRAME_DIR_RX, 0, 0);
   assert(channel && !strcmp(channel->codec, "pc16") && client.rx_channels[0]);
   memcpy(snapshot, media_channels, sizeof(snapshot));
   dict_add(request, "media.cmd", "codec");
   dict_add(request, "media.chan-uuid", channel->uuid);
   for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
      dict_add(request, "media.codec", invalid[i]);
      assert(!ws_handle_mediachan_msg(&client, request));
      assert(!memcmp(snapshot, media_channels, sizeof(snapshot)));
   }
   dict_free(request);
   dict_free(cfg);
   cfg = NULL;
   puts("PASS: subscribe-create and codec-select reject invalid/unnegotiated codecs without mutation");
   return 0;
}
