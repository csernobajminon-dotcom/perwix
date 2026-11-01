#ifndef PERWIX_DRIVER_H
#define PERWIX_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PERWIX_BUS_PLATFORM,
    PERWIX_BUS_PCI,
    PERWIX_BUS_USB,
} perwix_bus_t;

typedef struct {
    const char *name;
    perwix_bus_t bus;
    bool (*probe)(uint16_t vendor, uint16_t device);
    bool (*start)(void);
} perwix_driver_t;

bool driver_register(const perwix_driver_t *driver);
uint32_t driver_registered_count(void);

#endif
