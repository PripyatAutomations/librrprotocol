//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RR_PROTOCOL_WS_SERIAL_H
#define RR_PROTOCOL_WS_SERIAL_H
#include <librrprotocol/ws.binframe.h>
#define RR_SERIAL_FRAME_CODEC "seri"
#define RR_SERIAL_FRAME_EVENT "serial.frame"
#define RR_SERIAL_BLOCK_MAX 1024
#define RR_GPS_FRAME_CODEC "nmea"
#define RR_GPS_FRAME_EVENT "gps.nmea.frame"
// Binary data uses MODEM/seri, NA rig/VFO, an opened session-local stream,
// TX client -> device and RX device -> client. Payload is arbitrary raw bytes.
bool rr_serial_frame_valid(const struct rr_binframe *frame);
bool ws_handle_serial_cli_msg(rrconn_t *client, dict *message);
#endif
