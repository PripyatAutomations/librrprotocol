//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// rrserver/cfg.bridge.h: named bridge configuration.
#ifndef RRPROTOCOL_BRIDGE_CONFIG_H
#define RRPROTOCOL_BRIDGE_CONFIG_H
#include <stdbool.h>
bool rrp_bridge_config_init(void);
const char *rrp_bridge_config_get(const char *name, const char *key);
#endif
