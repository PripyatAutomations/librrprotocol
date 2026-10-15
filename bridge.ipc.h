/* Private local bridge control channel. One SOCK_SEQPACKET record per message. */
#ifndef RRPROTOCOL_BRIDGE_IPC_H
#define RRPROTOCOL_BRIDGE_IPC_H
#include <stdbool.h>
#include <stdint.h>
#define RRP_BRIDGE_IPC_MAGIC 0x52524231u
#define RRP_BRIDGE_IPC_VERSION 1u
#define RRP_BRIDGE_IPC_FD 3
#define RRP_BRIDGE_IPC_HELLO 1u
#define RRP_BRIDGE_IPC_CAPABILITIES 2u
#define RRP_BRIDGE_IPC_SHUTDOWN 3u
#define RRP_BRIDGE_IPC_MAX 512u
/* Fixed layout intentionally versioned; local, same-build bridge only. */
typedef struct rrp_bridge_ipc_message {
   uint32_t magic, version, type, reserved;
   char name[64];
   char driver[64];
   uint32_t flags; /* bit 0 RX, 1 TX, 2 RX audio, 3 IQ, 4 waterfall, 5 TX audio */
   uint32_t reserved2;
   uint64_t rx_min_hz, rx_max_hz, tx_min_hz, tx_max_hz;
   char rx_modes[96], tx_modes[96];
} rrp_bridge_ipc_message_t;
/* Nonblocking receive returns 1=message, 0=no message, -1=error/EOF. */
int rrp_bridge_ipc_recv(int fd, rrp_bridge_ipc_message_t *msg);
bool rrp_bridge_ipc_send(int fd, const rrp_bridge_ipc_message_t *msg);
void rrp_bridge_ipc_init(rrp_bridge_ipc_message_t *msg, uint32_t type);
#endif
