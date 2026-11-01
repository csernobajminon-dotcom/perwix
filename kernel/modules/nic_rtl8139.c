#include "netdev.h"

extern const nic_driver_t nic_driver_rtl8139;

bool perwix_module_init(void)
{
    return net_register_nic_driver(&nic_driver_rtl8139);
}
