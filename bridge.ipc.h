//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
/* Private local bridge control channel. One SOCK_SEQPACKET record per message. */
#ifndef RRPROTOCOL_BRIDGE_IPC_H
#define RRPROTOCOL_BRIDGE_IPC_H
#include <stdbool.h>
#include <stdint.h>
#define RRP_BRIDGE_IPC_MAGIC 0x52524231u
#define RRP_BRIDGE_IPC_VERSION 4u
#define RRP_BRIDGE_IPC_FD 3
#define RRP_BRIDGE_IPC_HELLO 1u
#define RRP_BRIDGE_IPC_CAPABILITIES 2u
#define RRP_BRIDGE_IPC_SHUTDOWN 3u
#define RRP_BRIDGE_IPC_SET 4u
#define RRP_BRIDGE_IPC_STATE 5u
#define RRP_BRIDGE_IPC_RESULT 6u
#define RRP_BRIDGE_IPC_HEARTBEAT 7u
#define RRP_BRIDGE_IPC_METADATA 8u
#define RRP_BRIDGE_IPC_LOG 9u
#define RRP_BRIDGE_IPC_WATERFALL 10u
/* WATERFALL control: available=enabled, flags=absolute zoom (0..14). */
#define RRP_BRIDGE_RX 1u
#define RRP_BRIDGE_TX 2u
#define RRP_BRIDGE_RX_AUDIO 4u
#define RRP_BRIDGE_RX_IQ 8u
#define RRP_BRIDGE_WATERFALL 16u
#define RRP_BRIDGE_TX_AUDIO 32u
#define RRP_BRIDGE_IPC_MAX 1024u
/* Fixed layout intentionally versioned; local, same-build bridge only. */
typedef struct rrp_bridge_ipc_message {
   uint32_t magic, version, type, reserved;
   char name[64];
   char driver[64];
   uint32_t flags;              /* bit 0 RX, 1 TX, 2 RX audio, 3 IQ, 4 waterfall, 5 TX audio */
   uint32_t reserved2;
   uint64_t rx_min_hz, rx_max_hz, tx_min_hz, tx_max_hz;
   char rx_modes[96], tx_modes[96];
   uint64_t request, frequency_hz;
   uint32_t available, success;
   char mode[16];
   char metadata_key[32], metadata_value[512];
} rrp_bridge_ipc_message_t;
/* Nonblocking receive returns 1=message, 0=no message, -1=error/EOF. */
int rrp_bridge_ipc_recv(int fd, rrp_bridge_ipc_message_t *msg);
bool rrp_bridge_ipc_send(int fd, const rrp_bridge_ipc_message_t *msg);
bool rrp_bridge_ipc_valid(const rrp_bridge_ipc_message_t *msg);
bool rrp_bridge_mode_valid(const char *modes, const char *mode);
void rrp_bridge_ipc_init(rrp_bridge_ipc_message_t *msg, uint32_t type);
#endif
