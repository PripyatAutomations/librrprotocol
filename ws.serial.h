//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#ifndef RR_PROTOCOL_WS_SERIAL_H
#define	RR_PROTOCOL_WS_SERIAL_H
#include <librrprotocol/ws.binframe.h>
#include <time.h>
#define	RR_SERIAL_FRAME_CODEC "seri"
#define	RR_SERIAL_FRAME_EVENT "serial.frame"
#define	RR_SERIAL_BLOCK_MAX 1024
#define	RR_NMEA_FRAME_CODEC "nmea"
#define	RR_NMEA_FRAME_EVENT "gps.nmea.frame"
#define	RR_GPS_FRAME_CODEC "gpsp"
#define	RR_GPS_FRAME_EVENT "gps.position.frame"
#define	RR_GPS_POSITION_PAYLOAD_LEN 9
#define	RR_GPS_POSITION_VALID 0x01
#define	RR_GPS_POSITION_MANUAL 0x02
// GPS position records use MODEM/gpsp, RX, NA VFO, and a media subscription
// stream. Payload is signed big-endian int32 latitude/longitude in 1e-7
// degrees followed by flags (valid/manual). Opt-in MODEM/nmea carries one
// checksum-valid sentence without CRLF (1-509 bytes). Raw serial uses MODEM/seri.
// Synthesize a checksum-correct GPRMC sentence (without CRLF) into caller buffer.
size_t rr_gps_nmea_rmc(int32_t latitude, int32_t longitude, uint8_t flags, time_t utc,
                       char *out, size_t capacity);
bool rr_serial_frame_valid(const struct rr_binframe *frame);
bool ws_handle_serial_cli_msg(rrconn_t *client, dict *message);
#endif
