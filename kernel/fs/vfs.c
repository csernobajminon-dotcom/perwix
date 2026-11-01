#include "vfs.h"

#define VFS_MAX_FILESYSTEMS 16U

static const perwix_fs_type_t *filesystems[VFS_MAX_FILESYSTEMS];
static uint32_t filesystem_count;
static const perwix_fs_type_t *root_filesystem;
static void *root_instance;
static char root_source[32];

static bool text_equal(const char *left, const char *right)
{
    if (left == 0 || right == 0) return false;
    while (*left != '\0' && *right != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static void copy_string(char *destination, const char *source, uint32_t size)
{
    uint32_t i = 0U;
    if (size == 0U) return;
    if (source != 0) {
        while (source[i] != '\0' && i + 1U < size) {
            destination[i] = source[i];
            ++i;
        }
    }
    destination[i] = '\0';
}

bool vfs_register(const perwix_fs_type_t *filesystem)
{
    if (filesystem == 0 || filesystem->name == 0 ||
        filesystem->mount == 0 || filesystem->list == 0 ||
        filesystem->read == 0 || filesystem_count >= VFS_MAX_FILESYSTEMS) {
        return false;
    }
    for (uint32_t i = 0; i < filesystem_count; ++i) {
        if (text_equal(filesystems[i]->name, filesystem->name)) return false;
    }
    filesystems[filesystem_count++] = filesystem;
    return true;
}

bool vfs_mount_root(const char *filesystem_name, const char *source)
{
    if (filesystem_name == 0 || source == 0) return false;
    for (uint32_t i = 0; i < filesystem_count; ++i) {
        if (text_equal(filesystems[i]->name, filesystem_name)) {
            void *instance = 0;
            if (!filesystems[i]->mount(source, &instance) || instance == 0) {
                return false;
            }
            root_filesystem = filesystems[i];
            root_instance = instance;
            copy_string(root_source, source, sizeof(root_source));
            return true;
        }
    }
    return false;
}

bool vfs_is_mounted(void)
{
    return root_filesystem != 0 && root_instance != 0;
}

const char *vfs_root_filesystem(void)
{
    return vfs_is_mounted() ? root_filesystem->name : "none";
}

const char *vfs_root_source(void)
{
    return vfs_is_mounted() ? root_source : "none";
}

bool vfs_list(const char *path, vfs_dirent_t *entries,
              uint32_t capacity, uint32_t *count)
{
    if (!vfs_is_mounted() || path == 0 || entries == 0 || count == 0 ||
        capacity == 0U) return false;
    return root_filesystem->list(root_instance, path, entries, capacity, count);
}

bool vfs_read(const char *path, uint64_t offset, void *buffer,
              uint32_t capacity, uint32_t *bytes_read, uint32_t *file_size)
{
    if (!vfs_is_mounted() || path == 0 || buffer == 0 ||
        bytes_read == 0 || file_size == 0) return false;
    return root_filesystem->read(root_instance, path, offset, buffer,
                                 capacity, bytes_read, file_size);
}

uint32_t vfs_registered_count(void)
{
    return filesystem_count;
}
