// rrserver/cfg.bridge.h: named bridge configuration.
#ifndef RRPROTOCOL_BRIDGE_CONFIG_H
#define RRPROTOCOL_BRIDGE_CONFIG_H
#include <stdbool.h>
bool rrp_bridge_config_init(void);
const char *rrp_bridge_config_get(const char *name, const char *key);
#endif
