/* Generic bounded RF/AF spectrum row contract.
 * Provider-independent: Kiwi /W/F, IQ FFT and future SDRs produce the same
 * versioned descriptor plus quantized logarithmic power samples.
 * Never reinterpret these values as PCM. */
#ifndef RRPROTOCOL_SPECTRUM_ROW_H
#define RRPROTOCOL_SPECTRUM_ROW_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define RRP_SPECTRUM_VERSION 1u
#define RRP_SPECTRUM_MAX_BINS 4096u
#define RRP_SPECTRUM_KIWI_BINS 1024u

enum rrp_spectrum_axis {
   RRP_SPECTRUM_AXIS_AF = 1,
   RRP_SPECTRUM_AXIS_RF = 2
};
enum rrp_spectrum_format {
   RRP_SPECTRUM_SP08 = 1
};

/* Frequencies describe the edges of the complete row, not the tuned carrier.
 * A bin center is low_hz + (i + 0.5) * (high_hz-low_hz) / bins.
 * Power is a provider-calibrated unsigned log level. Lower values are
 * darker; clients expose display gain/floor independently of DSP gain.
 * Sequence and epoch are per producing source. */
typedef struct rrp_spectrum_row {
   uint32_t version;
   uint32_t axis;
   uint32_t format;
   uint32_t bins;
   uint64_t low_hz;
   uint64_t high_hz;
   uint64_t sequence;
   uint64_t epoch;
   uint64_t monotonic_us;
   uint8_t levels[RRP_SPECTRUM_MAX_BINS];
} rrp_spectrum_row_t;

static inline bool rrp_spectrum_row_valid(const rrp_spectrum_row_t *row) {
   return row && row->version == RRP_SPECTRUM_VERSION &&
      (row->axis == RRP_SPECTRUM_AXIS_RF || row->axis == RRP_SPECTRUM_AXIS_AF) &&
      row->format == RRP_SPECTRUM_SP08 &&
      row->bins > 0 && row->bins <= RRP_SPECTRUM_MAX_BINS &&
      row->low_hz < row->high_hz && row->epoch != 0;
}

/* Kiwi /W/F uncompressed frames carry 1024 packed intensity bins.
 * Transport-specific header parsing and zoom/start validation must happen
 * in the Kiwi adapter, before calling this conversion. */
static inline bool rrp_spectrum_kiwi_row(rrp_spectrum_row_t *out,
      const uint8_t *pixels, size_t len, uint64_t low_hz, uint64_t high_hz,
      uint64_t sequence, uint64_t epoch, uint64_t monotonic_us) {
   if (!out || !pixels || len != RRP_SPECTRUM_KIWI_BINS) {
      return false;
   }
   *out = (rrp_spectrum_row_t) {
      .version = RRP_SPECTRUM_VERSION,
      .axis = RRP_SPECTRUM_AXIS_RF,
      .format = RRP_SPECTRUM_SP08,
      .bins = RRP_SPECTRUM_KIWI_BINS,
      .low_hz = low_hz,
      .high_hz = high_hz,
      .sequence = sequence,
      .epoch = epoch,
      .monotonic_us = monotonic_us
   };
   for (size_t i = 0; i < len; i++) {
      /* Kiwi reports inverted 8-bit logarithmic amplitude: darker/stronger
       * raw byte values represent greater signal strength. Normalize here. */
      out->levels[i] = (uint8_t)(255u - pixels[i]);
   }
   return rrp_spectrum_row_valid(out);
}
#endif
