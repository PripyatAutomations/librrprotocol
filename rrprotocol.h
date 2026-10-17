//      This is part of rustyrig-fw.
// https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#if     !defined(__rrprotocol_h)
#define __rrprotocol_h

#include "build_config.h"
#if     defined(USE_MONGOOSE)
#include "ext/libmongoose/mongoose.h"
#endif
#include <librrprotocol/vfo.h>
#include <librrprotocol/auth.h>
#include <librrprotocol/http.h>
//#include <librrprotocol/irc.h>
#include <librrprotocol/ws.h>
#include <librrprotocol/ws.binframe.h>
#include <librrprotocol/ws.mediachan.h>
#include <librrprotocol/state.h>
#include <librrprotocol/client-flags.h>
#include <librrprotocol/connman.h>
#include <librrprotocol/server.url.h>
#include <librrprotocol/cfg.fwdsp.h>
extern const char *server_name;

// WebSocket room membership. The authoritative room is named from the
// configured station and rig; media subscriptions are independent of rooms.
extern const char *ws_authoritative_room(void);
extern const char *ws_site_room(void);
bool ws_room_rig_base(const char *room);
bool ws_room_rig_namespace(const char *room);
bool ws_room_same_rig(const char *room, const char *base);
bool ws_room_tx_control(const char *room);
bool ws_room_rx_tunable(const char *room);
bool ws_room_set_rx_tuning(const char *room, bool enabled);
bool ws_room_set_rx_tuning_mask(const char *room, uint32_t mask);
uint32_t ws_room_rx_tuning_mask(const char *room);
bool ws_room_control_allowed(rrconn_t *client, const char *room, bool frequency);
bool ws_send_ptt_cmd_in_room(rrconn_t *, const char *vfo, bool ptt, const char *room);
bool ws_send_freq_cmd_in_room(rrconn_t *, const char *vfo, long freq, const char *room);
bool ws_send_mode_cmd_in_room(rrconn_t *, const char *vfo, const char *mode, const char *room);
bool ws_send_width_cmd_in_room(rrconn_t *, const char *vfo, const char *width, const char *room);

extern bool ws_room_name_valid(const char *room);
extern bool ws_room_station_scoped(const char *room);
extern bool ws_room_set_vfo_mask(const char *room, uint32_t mask);
/* Client-side room identity learned from the server's authenticated room event. */
extern void ws_set_authoritative_room(const char *room);
extern bool ws_client_in_room(const rrconn_t *cptr, const char *room);
// Synchronous server policy check; a binary listener may set allowed=false.
#define RR_ROOM_JOIN_CHECK_EVENT "protocol.room.join.check"
typedef struct rr_room_join_check {
   const char *room;
   bool allowed;
} rr_room_join_check_t;
extern bool ws_client_join_room(rrconn_t *cptr, const char *room);
extern bool ws_client_part_room(rrconn_t *cptr, const char *room);
extern void ws_broadcast_room_dict(rrconn_t *sender, dict *d, const char *room);
extern bool ws_room_has_vfos(const char *room);
extern uint32_t ws_room_vfo_mask(const char *room);
extern void ws_set_authoritative_vfo_mask(uint32_t mask);

// Start the server's persistent callsign lookup helper after radio setup.
extern bool ws_callsign_lookup_init(void);
extern void ws_callsign_lookup_poll(void);
extern bool ws_handle_callsign_msg(rrconn_t *cptr, dict *d);

#endif // !defined(__rrprotocol_h)
