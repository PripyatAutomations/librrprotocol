//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
#include <librrprotocol/bridge.ipc.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>

_Static_assert(sizeof(rrp_bridge_ipc_message_t) <= RRP_BRIDGE_IPC_MAX, "bridge IPC size");

static bool token(const char *s) {
   if (!s || !*s) {
      return false;
   }

   for ( ; *s ; s++) {
      if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-') {
         return false;
      }
   }

   return true;
}
static bool modes_valid(const char *s) {
   for ( ; *s ; s++) {
      if (!isalnum((unsigned char)*s) && *s != ' ' && *s != '-') {
         return false;
      }
   }

   return true;
}
bool rrp_bridge_mode_valid(const char *modes, const char *mode) {
   if (!modes || !token(mode)) {
      return false;
   }
   size_t len = strlen(mode);

   for (const char *p = modes ; *p ; ) {
      while (*p == ' ') {
         p++;
      }
      size_t n = strcspn(p, " ");

      if (n == len && !strncmp(p, mode, n)) {
         return true;
      }
      p += n;
   }

   return false;
}
bool rrp_bridge_ipc_valid(const rrp_bridge_ipc_message_t *m) {
   if (!m || m->magic != RRP_BRIDGE_IPC_MAGIC || m->version != RRP_BRIDGE_IPC_VERSION ||
      m->reserved || m->reserved2 || m->type < RRP_BRIDGE_IPC_HELLO || m->type > RRP_BRIDGE_IPC_METADATA ||
      !memchr(m->name, 0, sizeof(m->name)) || !memchr(m->driver, 0, sizeof(m->driver)) ||
      !memchr(m->rx_modes, 0, sizeof(m->rx_modes)) || !memchr(m->tx_modes, 0, sizeof(m->tx_modes)) ||
      !memchr(m->mode, 0, sizeof(m->mode)) || !token(m->name) || !token(m->driver) || m->available > 1 || m->success > 1) {
      return false;
   }

   if (m->type == RRP_BRIDGE_IPC_METADATA &&
      (!memchr(m->metadata_key, 0, sizeof(m->metadata_key)) || !memchr(m->metadata_value, 0, sizeof(m->metadata_value)) ||
      !token(m->metadata_key) || !m->metadata_value[0])) {
      return false;
   }

   if (m->type == RRP_BRIDGE_IPC_CAPABILITIES) {
      if ((m->flags & ~63u) || !modes_valid(m->rx_modes) || !modes_valid(m->tx_modes) ||
         ((m->flags & RRP_BRIDGE_RX) && (m->rx_min_hz > m->rx_max_hz || !m->rx_modes[0])) ||
         ((m->flags & RRP_BRIDGE_TX) && (m->tx_min_hz > m->tx_max_hz || !m->tx_modes[0])) ||
         (!(m->flags & RRP_BRIDGE_RX) && (m->flags & (RRP_BRIDGE_RX_AUDIO | RRP_BRIDGE_RX_IQ | RRP_BRIDGE_WATERFALL))) ||
         (!(m->flags & RRP_BRIDGE_TX) && (m->flags & RRP_BRIDGE_TX_AUDIO))) {
         return false;
      }
   }

   if (m->type == RRP_BRIDGE_IPC_SET && (!m->request || !token(m->mode))) {
      return false;
   }

   if (m->type == RRP_BRIDGE_IPC_STATE && m->available && !token(m->mode)) {
      return false;
   }

   if (m->type == RRP_BRIDGE_IPC_RESULT && !m->request) {
      return false;
   }

   return true;
}
void rrp_bridge_ipc_init(rrp_bridge_ipc_message_t *m, uint32_t type) {
   memset(m, 0, sizeof(*m));
   m->magic = RRP_BRIDGE_IPC_MAGIC;
   m->version = RRP_BRIDGE_IPC_VERSION;
   m->type = type;
}
bool rrp_bridge_ipc_send(int fd, const rrp_bridge_ipc_message_t *m) {
   if (fd < 0 || !rrp_bridge_ipc_valid(m)) {
      return false;
   }
   ssize_t n;
   do {
      n = send(fd, m, sizeof(*m), MSG_NOSIGNAL | MSG_DONTWAIT);
   } while (n < 0 && errno == EINTR);
   return n == (ssize_t) sizeof(*m);
}
int rrp_bridge_ipc_recv(int fd, rrp_bridge_ipc_message_t *m) {
   if (fd < 0 || !m) {
      return -1;
   }
   ssize_t n = recv(fd, m, sizeof(*m), MSG_DONTWAIT | MSG_TRUNC);

   if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
      return 0;
   }

   return n == (ssize_t) sizeof(*m) && rrp_bridge_ipc_valid(m) ? 1 : -1;
}
