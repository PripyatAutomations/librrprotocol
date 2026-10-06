//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <librustyaxe/core.h>
#include <librrprotocol/rrprotocol.h>
#include <librrprotocol/ws.serial.h>
static void gps_angle(int32_t angle, unsigned width, char *out, size_t capacity) {
   uint32_t absolute = angle < 0 ? -(int64_t)angle : angle;
   unsigned degrees = absolute / 10000000;
   uint64_t minutes = ((uint64_t)(absolute % 10000000) * 60 + 5) / 10;
   if (minutes == 60000000) { degrees++; minutes = 0; }
   snprintf(out, capacity, "%0*u%02u.%06u", width, degrees,
      (unsigned)(minutes / 1000000), (unsigned)(minutes % 1000000));
}
size_t rr_gps_nmea_rmc(int32_t latitude, int32_t longitude, uint8_t flags, time_t utc,
                       char *out, size_t capacity) {
   if (!out || capacity < 8 || latitude < -900000 || latitude > 900000 ||
       longitude < -1800000 || longitude > 1800000 ||
       (flags & ~(RR_GPS_POSITION_VALID | RR_GPS_POSITION_MANUAL))) return 0;
   struct tm gps_tm;
   if (!gmtime_r(&utc, &gps_tm)) return 0;
   char time_text[16], date_text[16], lat[24], lon[24];
   if (!strftime(time_text, sizeof(time_text), "%H%M%S", &gps_tm) ||
       !strftime(date_text, sizeof(date_text), "%d%m%y", &gps_tm)) return 0;
   int len;
   if (flags & RR_GPS_POSITION_VALID) {
      gps_angle(latitude, 2, lat, sizeof(lat)); gps_angle(longitude, 3, lon, sizeof(lon));
      len = snprintf(out, capacity, "$GPRMC,%s,A,%s,%c,%s,%c,0.0,,%s,,,%c", time_text,
         lat, latitude < 0 ? 'S' : 'N', lon, longitude < 0 ? 'W' : 'E', date_text,
         flags & RR_GPS_POSITION_MANUAL ? 'M' : 'A');
   } else {
      len = snprintf(out, capacity, "$GPRMC,%s,V,,,,,0.0,,%s,,,N", time_text, date_text);
   }
   if (len < 0 || (size_t)len + 4 > capacity) return 0;
   unsigned checksum = 0;
   for (int i = 1; i < len; i++) checksum ^= (unsigned char)out[i];
   int suffix = snprintf(out + len, capacity - (size_t)len, "*%02X", checksum);
   return suffix == 3 ? (size_t)len + (size_t)suffix : 0;
}
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
