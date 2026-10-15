#include "bridge.sdr.h"
#include <string.h>

// A single driver per isolated rrbridge process. No global device instance.
static const rrp_sdr_driver_t *registered_driver;

bool rrp_sdr_register(const rrp_sdr_driver_t *driver) {
   if (!driver || driver->abi_version != RRP_SDR_ABI_VERSION ||
       !driver->name || !driver->create || !driver->destroy ||
       !driver->capabilities || !driver->start || !driver->poll ||
       !driver->set_receiver || registered_driver)
      return true;
   registered_driver = driver;
   return false;
}

void rrp_sdr_unregister(const rrp_sdr_driver_t *driver) {
   if (registered_driver == driver)
      registered_driver = NULL;
}

const rrp_sdr_driver_t *rrp_sdr_find(const char *name) {
   return registered_driver && name &&
          strcmp(registered_driver->name, name) == 0 ? registered_driver : NULL;
}
