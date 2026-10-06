// Rejected codec requests must not create channels or change their format.
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.mediachan.h>

time_t now;
bool dying, restarting;
extern bool ws_binframe_process_mg(rrconn_t *, const char *, size_t);

int main(void) {
   cfg = dict_new();
   dict_add(cfg, "codecs.allowed", "pc16 mu08 opuT ---- none");
   dict_add(cfg, "audio.test-mode", "true");
   http_user_t *account = &http_users[1];
   account->uid = 1;
   snprintf(account->privs, sizeof(account->privs), "admin,rx,tx");
   rrconn_t client = { .authenticated = true, .user = account };
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
   // Direction-specific account privileges apply even to no-op codec selections.
   dict_add(request, "media.codec", "pc16");
   snprintf(account->privs, sizeof(account->privs), "tx");
   assert(!ws_handle_mediachan_msg(&client, request));
   snprintf(account->privs, sizeof(account->privs), "rx");
   assert(ws_handle_mediachan_msg(&client, request));
   struct rr_mediachan *tx = media_chan_add(RR_BINFRAME_SUBSYS_AUDIO, RR_BINFRAME_DIR_TX, 0, 0, "pc16", "TX");
   assert(tx);
   dict_add(request, "media.chan-uuid", tx->uuid);
   assert(!ws_handle_mediachan_msg(&client, request));
   snprintf(account->privs, sizeof(account->privs), "tx");
   assert(ws_handle_mediachan_msg(&client, request));
   snprintf(account->privs, sizeof(account->privs), "admin");
   assert(!ws_handle_mediachan_msg(&client, request));
   snprintf(account->privs, sizeof(account->privs), "view,chat");
   memcpy(snapshot, media_channels, sizeof(snapshot));
   dict_add(request, "media.cmd", "subscribe");
   dict_add(request, "media.chan-uuid", "unknown");
   dict_add_int(request, "media.vfo", 240);
   assert(!ws_handle_mediachan_msg(&client, request));
   assert(!memcmp(snapshot, media_channels, sizeof(snapshot)));
   dict_add(request, "media.cmd", "codec");
   snprintf(account->privs, sizeof(account->privs), "rx,tx");
   // Already keyed sessions must lose audio authority when their account changes.
   snprintf(client.codec_tx, sizeof(client.codec_tx), "pc16");
   client.is_ptt = true; client.ptt_vfo = 'A';
   client.tx_channels[0] = (uint32_t)(tx - media_channels) + 1;
   uint8_t *packet = NULL;
   const uint8_t pcm[2] = {0, 0};
   int length = rr_binframe_frame(&packet, RR_BINFRAME_SUBSYS_AUDIO, "pc16", RR_BINFRAME_DIR_TX, 0, 0, 1, 1, 0, pcm, sizeof(pcm));
   assert(length > 0);
   assert(ws_binframe_process_mg(&client, (const char *)packet, length));
   snprintf(account->privs, sizeof(account->privs), "view,chat");
   client_set_flag(&client, FLAG_MEDIA_SOURCE);
   assert(!ws_binframe_process_mg(&client, (const char *)packet, length));
   snprintf(account->privs, sizeof(account->privs), "rx,video-src");
   client_set_flag(&client, FLAG_VIDEO_SOURCE);
   assert(!ws_binframe_process_mg(&client, (const char *)packet, length));
   snprintf(account->privs, sizeof(account->privs), "tx");
   account->is_muted = true;
   assert(!ws_binframe_process_mg(&client, (const char *)packet, length));
   account->is_muted = false;
   snprintf(account->privs, sizeof(account->privs), "noob");
   assert(!ws_binframe_process_mg(&client, (const char *)packet, length));
   snprintf(account->privs, sizeof(account->privs), "rx,tx");
   free(packet);
   struct rr_mediachan *gps = media_chan_add(RR_BINFRAME_SUBSYS_MODEM,
      RR_BINFRAME_DIR_RX, RR_BINFRAME_VFO_NA, 0, "gpsp", "position");
   struct rr_mediachan *nmea = media_chan_add(RR_BINFRAME_SUBSYS_MODEM,
      RR_BINFRAME_DIR_RX, RR_BINFRAME_VFO_NA, 0, "nmea", "receiver");
   assert(gps && nmea && gps != nmea && strcmp(gps->uuid,nmea->uuid));
   assert(media_chan_add(RR_BINFRAME_SUBSYS_MODEM,RR_BINFRAME_DIR_RX,
      RR_BINFRAME_VFO_NA,0,"nmea",NULL) == nmea);
   dict_add(request,"media.chan-uuid",gps->uuid);
   dict_add(request,"media.codec","pc16");
   assert(!ws_handle_mediachan_msg(&client,request));
   assert(!strcmp(gps->codec,"gpsp"));
   dict_add(request,"media.chan-uuid",nmea->uuid);
   assert(!ws_handle_mediachan_msg(&client,request));
   assert(!strcmp(nmea->codec,"nmea"));
   dict_free(request);
   dict_free(cfg);
   cfg = NULL;
   puts("PASS: subscribe-create and codec-select reject invalid/unnegotiated codecs without mutation");
   return 0;
}
