#include <librrprotocol/bridge.ipc.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void) {
   int fd[2];
   assert(!socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fd));
   rrp_bridge_ipc_message_t m, got;
   rrp_bridge_ipc_init(&m, RRP_BRIDGE_IPC_HELLO);
   snprintf(m.name, sizeof(m.name), "east");
   snprintf(m.driver, sizeof(m.driver), "mock");
   assert(rrp_bridge_ipc_recv(fd[1], &got) == 0);
   assert(rrp_bridge_ipc_send(fd[0], &m));
   assert(rrp_bridge_ipc_recv(fd[1], &got) == 1);
   assert(!memcmp(&got, &m, sizeof(m)));
   m.type = RRP_BRIDGE_IPC_CAPABILITIES;
   m.flags = RRP_BRIDGE_RX;
   m.rx_min_hz = 100000; m.rx_max_hz = 30000000;
   strcpy(m.rx_modes, "USB AM CW");
   assert(rrp_bridge_ipc_valid(&m));
   assert(rrp_bridge_mode_valid(m.rx_modes, "USB"));
   assert(!rrp_bridge_mode_valid(m.rx_modes, "US"));
   assert(!rrp_bridge_mode_valid(m.rx_modes, "USB AM"));
   m.rx_min_hz = 30000001;
   assert(!rrp_bridge_ipc_send(fd[0], &m));
   m.rx_min_hz = 100000;
   m.flags |= 128;
   assert(!rrp_bridge_ipc_valid(&m));
   m.flags = RRP_BRIDGE_TX_AUDIO;
   assert(!rrp_bridge_ipc_valid(&m));
   m.flags = RRP_BRIDGE_RX;
   memset(m.name, 'x', sizeof(m.name));
   assert(!rrp_bridge_ipc_valid(&m));
   strcpy(m.name, "east");
   m.type = RRP_BRIDGE_IPC_SET;
   assert(!rrp_bridge_ipc_valid(&m));
   m.request = 1; strcpy(m.mode, "USB");
   assert(rrp_bridge_ipc_valid(&m));
   m.version++;
   assert(send(fd[0], &m, sizeof(m), 0) == sizeof(m));
   assert(rrp_bridge_ipc_recv(fd[1], &got) == -1);
   m.version--;
   char oversize[RRP_BRIDGE_IPC_MAX + 1] = {0};
   memcpy(oversize, &m, sizeof(m));
   assert(send(fd[0], oversize, sizeof(oversize), 0) == sizeof(oversize));
   assert(rrp_bridge_ipc_recv(fd[1], &got) == -1);
   assert(send(fd[0], &m, 8, 0) == 8);
   assert(rrp_bridge_ipc_recv(fd[1], &got) == -1);
   assert(rrp_bridge_ipc_send(fd[0], &m));
   assert(rrp_bridge_ipc_recv(fd[1], &got) == 1);
   close(fd[0]);
   assert(rrp_bridge_ipc_recv(fd[1], &got) == -1);
   close(fd[1]);
   puts("PASS: bridge IPC records, bounds, truncation, versions and validation");
   return 0;
}
