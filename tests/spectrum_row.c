#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <librrprotocol/spectrum.row.h>

int main(void) {
   uint8_t pixels[RRP_SPECTRUM_KIWI_BINS];
   memset(pixels, 200, sizeof(pixels));
   pixels[500] = 35;
   rrp_spectrum_row_t row;
   assert(rrp_spectrum_kiwi_row(&row, pixels, sizeof(pixels),
      14000000, 14100000, 10, 3, 123456));
   assert(row.axis == RRP_SPECTRUM_AXIS_RF);
   assert(row.bins == 1024 && row.low_hz == 14000000);
   assert(row.levels[500] > row.levels[499]);
   assert(!rrp_spectrum_kiwi_row(&row, pixels, 200, 0, 30000000, 1, 1, 0));
   assert(!rrp_spectrum_kiwi_row(&row, pixels, sizeof(pixels), 100, 100, 1, 1, 0));
   assert(!rrp_spectrum_kiwi_row(&row, pixels, sizeof(pixels), 100, 1000, 1, 0, 0));
   puts("PASS: RF spectrum normalization, bounds and epochs");
   return 0;
}
