#ifndef PERWIX_VFS_H
#define PERWIX_VFS_H

#include <stdbool.h>
#include <stdint.h>

#define VFS_NAME_MAX 768U
#define VFS_MAX_DIR_ENTRIES 64U

typedef struct {
    char name[VFS_NAME_MAX];
    bool is_directory;
    uint32_t size;
} vfs_dirent_t;

typedef struct {
    const char *name;
    uint32_t version;
    bool (*mount)(const char *source, void **instance);
    bool (*list)(void *instance, const char *path, vfs_dirent_t *entries,
                 uint32_t capacity, uint32_t *count);
    bool (*read)(void *instance, const char *path, uint64_t offset,
                 void *buffer, uint32_t capacity, uint32_t *bytes_read,
                 uint32_t *file_size);
} perwix_fs_type_t;

bool vfs_register(const perwix_fs_type_t *filesystem);
bool vfs_mount_root(const char *filesystem_name, const char *source);
bool vfs_is_mounted(void);
const char *vfs_root_filesystem(void);
const char *vfs_root_source(void);
bool vfs_list(const char *path, vfs_dirent_t *entries,
              uint32_t capacity, uint32_t *count);
bool vfs_read(const char *path, uint64_t offset, void *buffer,
              uint32_t capacity, uint32_t *bytes_read, uint32_t *file_size);
uint32_t vfs_registered_count(void);

#endif
