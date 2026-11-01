#ifndef PERWIX_FAT32_H
#define PERWIX_FAT32_H

#include <stdbool.h>

/* Registers the read-only FAT32 driver and mounts the first valid FAT32 volume. */
bool fat32_init(void);
bool fat32_is_mounted(void);

#endif
