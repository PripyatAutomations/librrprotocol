//      This is part of rustyrig-fw. https://github.com/pripyatautomations/rustyrig-fw
//
// Do not pay money for this, except donations to the project, if you wish to.
// The software is not for sale. It is freely available, always.
//
// Licensed under MIT license, if built without mongoose or GPL if built with.
// Named SDR subprocess configuration sections.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <librustyaxe/core.h>
#include <librrprotocol/bridge.config.h>

static bool valid_name(const char *s) {
   if (!s || !*s || strlen(s) >= 64) {
      return false;
   }

   for ( ; *s ; s++) {
      if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-') {
         return false;
      }
   }

   return true;
}

static bool section_cb(const char *path, int line, const char *section, const char *buf) {
   if (!section || !buf) {
      return true;
   }
   const char *name = NULL;

   if (!strcmp(section, "bridge")) {
      name = NULL;
   } else if (!strncmp(section, "bridge:", 7) && valid_name(section + 7)) {
      name = section + 7;
   } else {
      return true;
   }
   char *copy = strdup(buf);

   if (!copy) {
      return true;
   }
   char *value = strchr(copy, '=');

   if (!value) {
      free(copy);

      return true;
   }
   *value++ = '\0';
   char *key = copy;
   while (isspace((unsigned char)*key)) {
      key++;
   }
   while (isspace((unsigned char)*value)) {
      value++;
   }
   char *end = key + strlen(key);
   while (end > key && isspace((unsigned char)end[-1])) {
      *--end = '\0';
   }
   end = value + strlen(value);
   while (end > value && isspace((unsigned char)end[-1])) {
      *--end = '\0';
   }
   /* [bridge] defines the roster and process path; named sections carry per-device parameters. Preserve unknown named keys to allow driver-specific settings.
    */
   bool key_valid = *key && strlen(key) <= 63;

   for (const char *c = key ; *c ; c++) {
      if (!isalnum((unsigned char)*c) && *c != '_' && *c != '-' && *c != '.') {
         key_valid = false;
      }
   }

   const char *hint_keys[] = {
      "rx-min-hz", "rx-max-hz", "rx-modes", "rx-audio", "rx-iq", "rx-waterfall",
      "tx-supported", "tx-min-hz", "tx-max-hz", "tx-modes", "tx-audio"
   };
   bool hint = false;

   for (unsigned i = 0 ; i < sizeof(hint_keys) / sizeof(hint_keys[0]) ; i++) {
      if (!strcmp(key, hint_keys[i])) {
         hint = true;
      }
   }

   if (!key_valid || (!name && !hint && strcmp(key, "instances") && strcmp(key, "path") && strcmp(key, "modules") && strcmp(key, "frequency")
      && strcmp(key, "mode") && strcmp(key, "host") && strcmp(key, "port") && strcmp(key, "password"))) {
      Log(LOG_CRIT, "cfg.bridge", "Invalid key in [%s] at %s:%d", section, path, line);
      free(copy);

      return true;
   }
   char fullkey[256];
   int n = name ? snprintf(fullkey, sizeof(fullkey), "bridge:%s.%s", name, key)
      : snprintf(fullkey, sizeof(fullkey), "bridge.%s", key);
   bool ok = n > 0 && (size_t)n < sizeof(fullkey) && dict_add(cfg, fullkey, value) == 0;
   free(copy);

   return !ok;
}

bool rrp_bridge_config_init(void) {
   return cfg_add_callback(NULL, "bridge", section_cb) && cfg_add_callback(NULL, "bridge:*", section_cb);
}

const char *rrp_bridge_config_get(const char *name, const char *key) {
   if (!valid_name(name) || !key || !*key || strlen(key) > 63) {
      return NULL;
   }
   char fullkey[256];
   int n = snprintf(fullkey, sizeof(fullkey), "bridge:%s.%s", name, key);

   return n > 0 && (size_t)n < sizeof(fullkey) ? cfg_get(fullkey) : NULL;
}
