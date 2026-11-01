#ifndef PERWIX_STORAGE_H
#define PERWIX_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

#define STORAGE_MAX_DISKS 4U
#define STORAGE_MAX_PARTITIONS 32U

typedef struct {
    bool present;
    uint8_t channel;       /* 0 = primary, 1 = secondary */
    uint8_t device;        /* 0 = master, 1 = slave */
    bool gpt;
    uint64_t sectors;
    uint32_t partition_count;
    char model[41];
} storage_disk_info_t;

typedef struct {
    uint64_t first_lba;
    uint64_t sectors;
    uint8_t mbr_type;
    bool bootable;
    const char *type_name;
    const char *filesystem;
} storage_partition_info_t;

void storage_init(void);
void storage_rescan(void);
uint32_t storage_disk_count(void);
bool storage_get_disk(uint32_t index, storage_disk_info_t *result);
uint32_t storage_get_partition_count(uint32_t disk_index);
bool storage_get_partition(uint32_t disk_index, uint32_t partition_index,
                           storage_partition_info_t *result);
bool storage_read_sectors(uint32_t disk_index, uint64_t lba,
                          uint8_t sector_count, void *buffer);

#endif
