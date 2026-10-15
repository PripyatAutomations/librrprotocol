#include <librrprotocol/bridge.ipc.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
void rrp_bridge_ipc_init(rrp_bridge_ipc_message_t *m, uint32_t type) {
   memset(m, 0, sizeof(*m));
   m->magic = RRP_BRIDGE_IPC_MAGIC;
   m->version = RRP_BRIDGE_IPC_VERSION;
   m->type = type;
}
bool rrp_bridge_ipc_send(int fd, const rrp_bridge_ipc_message_t *m) {
   return fd >= 0 && m && send(fd, m, sizeof(*m), MSG_NOSIGNAL | MSG_DONTWAIT) == (ssize_t)sizeof(*m);
}
int rrp_bridge_ipc_recv(int fd, rrp_bridge_ipc_message_t *m) {
   if (fd < 0 || !m) return -1;
   ssize_t n = recv(fd, m, sizeof(*m), MSG_DONTWAIT | MSG_TRUNC);
   if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
   if (n != (ssize_t)sizeof(*m) || m->magic != RRP_BRIDGE_IPC_MAGIC || m->version != RRP_BRIDGE_IPC_VERSION) return -1;
   if (!memchr(m->name, 0, sizeof(m->name)) || !memchr(m->driver, 0, sizeof(m->driver)) ||
       !memchr(m->rx_modes, 0, sizeof(m->rx_modes)) || !memchr(m->tx_modes, 0, sizeof(m->tx_modes))) return -1;
   return 1;
}
