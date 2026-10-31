#ifndef PERWIX_VFS_H
#define PERWIX_VFS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *name;
    uint32_t version;
    bool (*mount)(const char *source, const char *target);
} perwix_fs_type_t;

bool vfs_register(const perwix_fs_type_t *filesystem);
uint32_t vfs_registered_count(void);

#endif
