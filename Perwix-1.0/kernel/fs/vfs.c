#include "vfs.h"

#define VFS_MAX_FILESYSTEMS 16

static const perwix_fs_type_t *filesystems[VFS_MAX_FILESYSTEMS];
static uint32_t filesystem_count;

bool vfs_register(const perwix_fs_type_t *filesystem)
{
    if (filesystem == 0 || filesystem->name == 0 ||
        filesystem_count == VFS_MAX_FILESYSTEMS) {
        return false;
    }
    for (uint32_t i = 0; i < filesystem_count; ++i) {
        const char *left = filesystems[i]->name;
        const char *right = filesystem->name;
        uint32_t j = 0;
        while (left[j] != '\0' && left[j] == right[j]) {
            ++j;
        }
        if (left[j] == '\0' && right[j] == '\0') {
            return false;
        }
    }
    filesystems[filesystem_count++] = filesystem;
    return true;
}

uint32_t vfs_registered_count(void)
{
    return filesystem_count;
}
