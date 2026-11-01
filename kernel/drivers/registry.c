#include "driver.h"

#define DRIVER_REGISTRY_CAPACITY 64

static const perwix_driver_t *drivers[DRIVER_REGISTRY_CAPACITY];
static uint32_t driver_count;

bool driver_register(const perwix_driver_t *driver)
{
    if (driver == 0 || driver->name == 0 ||
        driver_count == DRIVER_REGISTRY_CAPACITY) {
        return false;
    }
    drivers[driver_count++] = driver;
    return true;
}

uint32_t driver_registered_count(void)
{
    return driver_count;
}
