#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.serial.h>
bool rr_serial_frame_valid(const struct rr_binframe *f) {
   return f && f->hdr.subsystem == RR_BINFRAME_SUBSYS_MODEM &&
      !memcmp(f->hdr.codec, RR_SERIAL_FRAME_CODEC, 4) &&
      f->hdr.vfo == RR_BINFRAME_VFO_NA && f->hdr.rig == RR_BINFRAME_RIG_NA &&
      f->hdr.stream && f->len && f->len <= RR_SERIAL_BLOCK_MAX &&
      (f->hdr.direction == RR_BINFRAME_DIR_TX || f->hdr.direction == RR_BINFRAME_DIR_RX);
}
bool ws_handle_serial_cli_msg(rrconn_t *client, dict *message) {
   (void)client; (void)message;
   // cli.main already emitted ws.msg.serial; application owns endpoints/UI.
   return false;
}
