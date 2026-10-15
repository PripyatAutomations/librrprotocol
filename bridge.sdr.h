// Shared receiver-driver ABI. Drivers run inside an isolated rrbridge process.
#ifndef RRPROTOCOL_BRIDGE_SDR_H
#define RRPROTOCOL_BRIDGE_SDR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RRP_SDR_ABI_VERSION 2u

typedef struct rrp_sdr_host {
   void (*audio_pcm16)(void *user, const int16_t *samples, size_t count,
                       unsigned sample_rate);
   void (*status)(void *user, const char *message);
   void *user;
} rrp_sdr_host_t;

/* Hardware capabilities, not authorization or instantaneous availability.
 * RX/TX must be checked independently. A bridge may report TX even when
 * rrserver policy forbids it; neither implies permission to transmit.
 * mode strings use the same space-separated spelling as property.enum.
 * The caller must not free strings; they remain owned by the driver.
 */
typedef struct rrp_sdr_capabilities {
   bool rx;
   bool tx;
   bool rx_audio;
   bool rx_iq;
   bool rx_waterfall;
   bool tx_audio;
   uint64_t rx_min_hz, rx_max_hz;
   uint64_t tx_min_hz, tx_max_hz;
   const char *rx_modes;
   const char *tx_modes;
} rrp_sdr_capabilities_t;

typedef struct rrp_sdr_driver {
   uint32_t abi_version;
   const char *name;
   // Each opened receiver has independent state; opaque instance belongs to driver.
   void *(*create)(const rrp_sdr_host_t *host);
   void (*destroy)(void *instance);
   bool (*capabilities)(void *instance, rrp_sdr_capabilities_t *out);
   // Transport-independent commands; the host owns socket and framing.
   bool (*tune)(void *instance, uint64_t hz, const char *mode,
                char *command, size_t capacity);
   // Incoming WebSocket payload, NOT a raw TCP stream.
   bool (*receive)(void *instance, const void *frame, size_t length);
} rrp_sdr_driver_t;

// false = success, matching librustyaxe's module convention.
bool rrp_sdr_register(const rrp_sdr_driver_t *driver);
void rrp_sdr_unregister(const rrp_sdr_driver_t *driver);
const rrp_sdr_driver_t *rrp_sdr_find(const char *name);
#endif
