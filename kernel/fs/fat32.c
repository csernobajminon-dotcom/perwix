#include "fat32.h"
#include "vfs.h"
#include "../storage/storage.h"
#include "../lib/utf8.h"

#define FAT32_SECTOR_SIZE 512U
#define FAT32_MAX_CLUSTER_HOPS 131072U
#define FAT32_LFN_UNITS 260U

#define FAT32_ATTR_DIRECTORY 0x10U
#define FAT32_ATTR_VOLUME_ID 0x08U
#define FAT32_ATTR_LFN 0x0fU

struct fat32_volume {
    uint32_t disk_index;
    uint32_t partition_index;
    uint64_t partition_lba;
    uint64_t partition_sectors;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t fat_count;
    uint32_t fat_sectors;
    uint32_t root_cluster;
    uint32_t cluster_count;
    uint64_t fat_start_lba;
    uint64_t data_start_lba;
    uint32_t total_sectors;
};

struct fat32_entry {
    char name[VFS_NAME_MAX];
    uint8_t attributes;
    uint32_t first_cluster;
    uint32_t size;
};

struct fat32_list_context {
    vfs_dirent_t *entries;
    uint32_t capacity;
    uint32_t count;
};

static struct fat32_volume mounted_volume;
static bool fat32_ready;
static uint8_t sector_buffer[FAT32_SECTOR_SIZE];

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void bytes_copy(void *destination, const void *source, uint32_t size)
{
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (uint32_t i = 0; i < size; ++i) out[i] = in[i];
}

static void text_copy(char *out, const char *in, uint32_t capacity)
{
    uint32_t i = 0U;
    if (capacity == 0U) return;
    while (in[i] != '\0' && i + 1U < capacity) {
        out[i] = in[i];
        ++i;
    }
    out[i] = '\0';
}

static bool text_equal(const char *a, const char *b)
{
    if (a == 0 || b == 0) return false;
    while (*a != '\0' && *b != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static bool name_equal(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        uint8_t ca = (uint8_t)*a++;
        uint8_t cb = (uint8_t)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (uint8_t)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (uint8_t)(cb + ('a' - 'A'));
        if (ca != cb) return false;
    }
    return *a == '\0' && *b == '\0';
}

static bool valid_cluster(const struct fat32_volume *volume, uint32_t cluster)
{
    return cluster >= 2U && cluster < volume->cluster_count + 2U &&
           cluster < 0x0ffffff0U;
}

static uint64_t cluster_lba(const struct fat32_volume *volume,
                            uint32_t cluster)
{
    return volume->data_start_lba +
           (uint64_t)(cluster - 2U) * volume->sectors_per_cluster;
}

static bool read_sector(struct fat32_volume *volume, uint64_t lba,
                        uint8_t *buffer)
{
    if (lba < volume->partition_lba ||
        lba - volume->partition_lba >= volume->partition_sectors ||
        lba - volume->partition_lba >= volume->total_sectors) {
        return false;
    }
    return storage_read_sectors(volume->disk_index, lba, 1U, buffer);
}

static bool fat_next_cluster(struct fat32_volume *volume, uint32_t cluster,
                             uint32_t *next_cluster)
{
    uint64_t byte_offset;
    uint64_t fat_lba;
    uint32_t offset;
    uint32_t next;

    if (!valid_cluster(volume, cluster) || next_cluster == 0) return false;
    byte_offset = (uint64_t)cluster * 4U;
    fat_lba = volume->fat_start_lba + byte_offset / FAT32_SECTOR_SIZE;
    offset = (uint32_t)(byte_offset % FAT32_SECTOR_SIZE);
    if (!read_sector(volume, fat_lba, sector_buffer)) return false;
    next = read_le32(sector_buffer + offset) & 0x0fffffffU;
    if (next >= 0x0ffffff8U) {
        *next_cluster = 0U; /* End of chain. */
        return true;
    }
    if (!valid_cluster(volume, next)) return false;
    *next_cluster = next;
    return true;
}

static uint8_t lfn_checksum(const uint8_t short_name[11])
{
    uint8_t sum = 0U;
    for (uint32_t i = 0; i < 11U; ++i) {
        sum = (uint8_t)(((sum & 1U) ? 0x80U : 0U) + (sum >> 1) + short_name[i]);
    }
    return sum;
}

static void append_utf8(char *out, uint32_t capacity, uint32_t *length,
                        uint32_t codepoint)
{
    char encoded[4];
    uint32_t count = utf8_encode_one(codepoint, encoded);
    if (count == 0U || *length + count >= capacity) return;
    for (uint32_t i = 0; i < count; ++i) out[(*length)++] = encoded[i];
}

static void lfn_to_utf8(const uint16_t *units, char *out, uint32_t capacity)
{
    uint32_t length = 0U;
    for (uint32_t i = 0; i < FAT32_LFN_UNITS; ++i) {
        uint32_t cp = units[i];
        if (cp == 0U || cp == 0xffffU) break;
        if (cp >= 0xd800U && cp <= 0xdbffU && i + 1U < FAT32_LFN_UNITS) {
            uint32_t low = units[i + 1U];
            if (low >= 0xdc00U && low <= 0xdfffU) {
                cp = 0x10000U + ((cp - 0xd800U) << 10) + (low - 0xdc00U);
                ++i;
            } else {
                cp = 0xfffdU;
            }
        } else if (cp >= 0xdc00U && cp <= 0xdfffU) {
            cp = 0xfffdU;
        }
        append_utf8(out, capacity, &length, cp);
    }
    out[length] = '\0';
}

static void short_name_to_text(const uint8_t *raw, char *out,
                               uint32_t capacity)
{
    uint32_t length = 0U;
    uint32_t base_end = 8U;
    uint32_t ext_end = 11U;
    while (base_end != 0U && raw[base_end - 1U] == ' ') --base_end;
    while (ext_end > 8U && raw[ext_end - 1U] == ' ') --ext_end;
    for (uint32_t i = 0; i < base_end; ++i) {
        uint8_t c = raw[i];
        if (i == 0U && c == 0x05U) c = 0xe5U;
        if (c < 0x20U || c >= 0x7fU) c = '?';
        if (length + 1U < capacity) out[length++] = (char)c;
    }
    if (ext_end > 8U && length + 1U < capacity) out[length++] = '.';
    for (uint32_t i = 8U; i < ext_end; ++i) {
        uint8_t c = raw[i];
        if (c < 0x20U || c >= 0x7fU) c = '?';
        if (length + 1U < capacity) out[length++] = (char)c;
    }
    out[length] = '\0';
}

typedef bool (*fat32_entry_callback)(const struct fat32_entry *entry,
                                     void *context);

static bool scan_directory(struct fat32_volume *volume, uint32_t first_cluster,
                           fat32_entry_callback callback, void *context)
{
    uint16_t lfn_units[FAT32_LFN_UNITS];
    bool lfn_active = false;
    uint8_t lfn_sum = 0U;
    uint32_t cluster = first_cluster;

    if (!valid_cluster(volume, cluster)) return false;

    for (uint32_t hop = 0; hop < FAT32_MAX_CLUSTER_HOPS; ++hop) {
        uint64_t base = cluster_lba(volume, cluster);
        for (uint32_t sector = 0; sector < volume->sectors_per_cluster; ++sector) {
            if (!read_sector(volume, base + sector, sector_buffer)) return false;
            for (uint32_t slot = 0; slot < 16U; ++slot) {
                const uint8_t *raw = sector_buffer + slot * 32U;
                uint8_t first = raw[0];
                uint8_t attributes = raw[11];
                if (first == 0U) return true;
                if (first == 0xe5U) {
                    lfn_active = false;
                    continue;
                }
                if (attributes == FAT32_ATTR_LFN) {
                    uint8_t ordinal = raw[0] & 0x1fU;
                    if ((raw[0] & 0x40U) != 0U) {
                        for (uint32_t i = 0; i < FAT32_LFN_UNITS; ++i) {
                            lfn_units[i] = 0xffffU;
                        }
                        lfn_active = ordinal != 0U && ordinal <= 20U;
                        lfn_sum = raw[13];
                    }
                    if (lfn_active && ordinal != 0U && ordinal <= 20U &&
                        raw[13] == lfn_sum) {
                        static const uint8_t positions[13] = {
                            1,3,5,7,9,14,16,18,20,22,24,28,30
                        };
                        uint32_t start = ((uint32_t)ordinal - 1U) * 13U;
                        if (start + 13U <= FAT32_LFN_UNITS) {
                            for (uint32_t i = 0; i < 13U; ++i) {
                                lfn_units[start + i] = read_le16(raw + positions[i]);
                            }
                        }
                    } else {
                        lfn_active = false;
                    }
                    continue;
                }
                if ((attributes & FAT32_ATTR_VOLUME_ID) != 0U) {
                    lfn_active = false;
                    continue;
                }

                struct fat32_entry entry;
                bool have_lfn = lfn_active && lfn_checksum(raw) == lfn_sum;
                if (have_lfn) {
                    lfn_to_utf8(lfn_units, entry.name, sizeof(entry.name));
                } else {
                    short_name_to_text(raw, entry.name, sizeof(entry.name));
                }
                lfn_active = false;
                if (entry.name[0] == '\0' || text_equal(entry.name, ".") ||
                    text_equal(entry.name, "..")) {
                    continue;
                }
                entry.attributes = attributes;
                entry.first_cluster = ((uint32_t)read_le16(raw + 20U) << 16) |
                                      read_le16(raw + 26U);
                entry.size = read_le32(raw + 28U);
                if (callback != 0 && !callback(&entry, context)) return true;
            }
        }
        uint32_t next;
        if (!fat_next_cluster(volume, cluster, &next)) return false;
        if (next == 0U) return true;
        cluster = next;
    }
    return false;
}

struct find_context {
    const char *wanted;
    struct fat32_entry *result;
    bool found;
};

static bool find_callback(const struct fat32_entry *entry, void *opaque)
{
    struct find_context *context = (struct find_context *)opaque;
    if (name_equal(entry->name, context->wanted)) {
        *context->result = *entry;
        context->found = true;
        return false;
    }
    return true;
}

static bool find_entry(struct fat32_volume *volume, uint32_t directory_cluster,
                       const char *name, struct fat32_entry *result)
{
    struct find_context context = { name, result, false };
    if (!scan_directory(volume, directory_cluster, find_callback, &context)) {
        return false;
    }
    return context.found;
}

static bool resolve_path(struct fat32_volume *volume, const char *path,
                         struct fat32_entry *result, bool *is_root)
{
    char component[VFS_NAME_MAX];
    uint32_t current = volume->root_cluster;
    uint32_t pos = 0U;
    bool found_any = false;

    if (path == 0) return false;
    while (path[pos] == '/') ++pos;
    while (path[pos] != '\0') {
        uint32_t length = 0U;
        while (path[pos] != '\0' && path[pos] != '/') {
            if (length + 1U >= sizeof(component)) return false;
            component[length++] = path[pos++];
        }
        component[length] = '\0';
        while (path[pos] == '/') ++pos;
        if (length == 0U) continue;
        if (text_equal(component, ".")) {
            if (path[pos] == '\0') {
                result->first_cluster = current;
                result->attributes = FAT32_ATTR_DIRECTORY;
                result->size = 0U;
                result->name[0] = '.';
                result->name[1] = '\0';
                found_any = true;
            }
            continue;
        }
        if (text_equal(component, "..")) {
            struct fat32_entry parent;
            if (find_entry(volume, current, "..", &parent) &&
                valid_cluster(volume, parent.first_cluster)) {
                current = parent.first_cluster;
            } else {
                current = volume->root_cluster;
            }
            result->first_cluster = current;
            result->attributes = FAT32_ATTR_DIRECTORY;
            result->size = 0U;
            result->name[0] = '.';
            result->name[1] = '.';
            result->name[2] = '\0';
            found_any = true;
            continue;
        }
        struct fat32_entry entry;
        if (!find_entry(volume, current, component, &entry)) return false;
        found_any = true;
        if (path[pos] != '\0') {
            if ((entry.attributes & FAT32_ATTR_DIRECTORY) == 0U ||
                !valid_cluster(volume, entry.first_cluster)) return false;
            current = entry.first_cluster;
        } else {
            *result = entry;
        }
    }
    if (is_root != 0) *is_root = !found_any;
    if (!found_any) {
        if (result != 0) {
            result->first_cluster = volume->root_cluster;
            result->attributes = FAT32_ATTR_DIRECTORY;
            result->size = 0U;
            result->name[0] = '/';
            result->name[1] = '\0';
        }
        return true;
    }
    return true;
}

static bool list_callback(const struct fat32_entry *entry, void *opaque)
{
    struct fat32_list_context *context = (struct fat32_list_context *)opaque;
    if (context->count >= context->capacity) return false;
    vfs_dirent_t *out = &context->entries[context->count++];
    text_copy(out->name, entry->name, sizeof(out->name));
    out->is_directory = (entry->attributes & FAT32_ATTR_DIRECTORY) != 0U;
    out->size = entry->size;
    return true;
}

static bool fat32_list(void *instance, const char *path,
                       vfs_dirent_t *entries, uint32_t capacity,
                       uint32_t *count)
{
    struct fat32_volume *volume = (struct fat32_volume *)instance;
    struct fat32_entry directory;
    bool is_root = false;
    if (volume == 0 || entries == 0 || capacity == 0U || count == 0 ||
        !resolve_path(volume, path, &directory, &is_root) ||
        (directory.attributes & FAT32_ATTR_DIRECTORY) == 0U ||
        !valid_cluster(volume, directory.first_cluster)) {
        return false;
    }
    (void)is_root;
    struct fat32_list_context context = { entries, capacity, 0U };
    if (!scan_directory(volume, directory.first_cluster, list_callback, &context)) {
        return false;
    }
    *count = context.count;
    return true;
}

static bool fat32_read(void *instance, const char *path, uint64_t offset,
                       void *buffer, uint32_t capacity, uint32_t *bytes_read,
                       uint32_t *file_size)
{
    struct fat32_volume *volume = (struct fat32_volume *)instance;
    struct fat32_entry file;
    uint32_t cluster;
    uint64_t cluster_bytes;
    uint64_t skip_clusters;
    uint64_t within_cluster;
    uint32_t copied = 0U;

    if (volume == 0 || path == 0 || buffer == 0 || bytes_read == 0 ||
        file_size == 0 || !resolve_path(volume, path, &file, 0) ||
        (file.attributes & FAT32_ATTR_DIRECTORY) != 0U) {
        return false;
    }
    *file_size = file.size;
    *bytes_read = 0U;
    if (file.size == 0U || offset >= file.size || capacity == 0U) return true;
    if (!valid_cluster(volume, file.first_cluster)) return false;

    uint32_t remaining = file.size - (uint32_t)offset;
    if (remaining > capacity) remaining = capacity;
    cluster_bytes = (uint64_t)volume->sectors_per_cluster * FAT32_SECTOR_SIZE;
    skip_clusters = offset / cluster_bytes;
    within_cluster = offset % cluster_bytes;
    cluster = file.first_cluster;

    if (skip_clusters > FAT32_MAX_CLUSTER_HOPS) return false;
    for (uint64_t hop = 0; hop < skip_clusters; ++hop) {
        uint32_t next;
        if (!fat_next_cluster(volume, cluster, &next) || next == 0U) return false;
        cluster = next;
    }

    uint32_t read_hops = 0U;
    while (copied < remaining) {
        if (++read_hops > FAT32_MAX_CLUSTER_HOPS * volume->sectors_per_cluster) {
            return false;
        }
        uint32_t sector_index = (uint32_t)(within_cluster / FAT32_SECTOR_SIZE);
        uint32_t sector_offset = (uint32_t)(within_cluster % FAT32_SECTOR_SIZE);
        uint64_t lba = cluster_lba(volume, cluster) + sector_index;
        if (!read_sector(volume, lba, sector_buffer)) return false;
        uint32_t take = FAT32_SECTOR_SIZE - sector_offset;
        if (take > remaining - copied) take = remaining - copied;
        bytes_copy((uint8_t *)buffer + copied, sector_buffer + sector_offset, take);
        copied += take;
        within_cluster += take;
        if (within_cluster >= cluster_bytes && copied < remaining) {
            uint32_t next;
            if (!fat_next_cluster(volume, cluster, &next) || next == 0U) return false;
            cluster = next;
            within_cluster = 0U;
        }
    }

    *bytes_read = copied;
    return true;
}

static bool fat32_mount(const char *source, void **instance)
{
    uint32_t disk_index = 0U;
    uint32_t partition_number = 0U;
    const char *p = source;
    static const char prefix[] = "disk";
    if (p == 0 || instance == 0) return false;
    for (uint32_t i = 0U; i < sizeof(prefix) - 1U; ++i) {
        if (*p == '\0' || *p != prefix[i]) return false;
        ++p;
    }
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') {
        uint32_t digit = (uint32_t)(*p++ - '0');
        if (digit >= STORAGE_MAX_DISKS ||
            disk_index > (STORAGE_MAX_DISKS - 1U - digit) / 10U) {
            return false;
        }
        disk_index = disk_index * 10U + digit;
    }
    if (*p++ != 'p' || *p < '1' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') {
        uint32_t digit = (uint32_t)(*p++ - '0');
        if (digit > STORAGE_MAX_PARTITIONS ||
            partition_number > (STORAGE_MAX_PARTITIONS - digit) / 10U) {
            return false;
        }
        partition_number = partition_number * 10U + digit;
    }
    if (*p != '\0' || partition_number == 0U) return false;
    --partition_number;

    storage_partition_info_t partition;
    if (!storage_get_partition(disk_index, partition_number, &partition) ||
        !text_equal(partition.filesystem, "FAT32")) return false;

    uint8_t boot[FAT32_SECTOR_SIZE];
    if (!storage_read_sectors(disk_index, partition.first_lba, 1U, boot) ||
        read_le16(boot + 11U) != FAT32_SECTOR_SIZE ||
        boot[13] == 0U || (boot[13] & (boot[13] - 1U)) != 0U ||
        boot[13] > 128U || read_le16(boot + 14U) == 0U ||
        boot[16] == 0U || boot[16] > 4U || read_le32(boot + 36U) == 0U ||
        read_le16(boot + 510U) != 0xaa55U) {
        return false;
    }

    uint32_t total = read_le16(boot + 19U);
    if (total == 0U) total = read_le32(boot + 32U);
    uint32_t reserved = read_le16(boot + 14U);
    uint32_t fats = boot[16];
    uint32_t fat_sectors = read_le32(boot + 36U);
    uint64_t overhead = (uint64_t)reserved + (uint64_t)fats * fat_sectors;
    if (total == 0U || total > partition.sectors || overhead >= total) return false;

    uint32_t cluster_count = (uint32_t)((total - overhead) / boot[13]);
    uint32_t root_cluster = read_le32(boot + 44U) & 0x0fffffffU;
    if (cluster_count == 0U || cluster_count >= 0x0ffffff0U ||
        root_cluster < 2U || root_cluster >= cluster_count + 2U ||
        (uint64_t)fat_sectors * FAT32_SECTOR_SIZE / 4U <
            (uint64_t)cluster_count + 2U) return false;

    mounted_volume.disk_index = disk_index;
    mounted_volume.partition_index = partition_number;
    mounted_volume.partition_lba = partition.first_lba;
    mounted_volume.partition_sectors = partition.sectors;
    mounted_volume.sectors_per_cluster = boot[13];
    mounted_volume.reserved_sectors = reserved;
    mounted_volume.fat_count = fats;
    mounted_volume.fat_sectors = fat_sectors;
    mounted_volume.root_cluster = root_cluster;
    mounted_volume.cluster_count = cluster_count;
    mounted_volume.fat_start_lba = partition.first_lba + reserved;
    mounted_volume.data_start_lba = mounted_volume.fat_start_lba +
                                    (uint64_t)fats * fat_sectors;
    mounted_volume.total_sectors = total;
    *instance = &mounted_volume;
    return true;
}

static const perwix_fs_type_t fat32_filesystem = {
    .name = "fat32",
    .version = 1U,
    .mount = fat32_mount,
    .list = fat32_list,
    .read = fat32_read
};

bool fat32_init(void)
{
    static bool registered;
    if (!registered) {
        if (!vfs_register(&fat32_filesystem)) return false;
        registered = true;
    }
    for (uint32_t disk = 0; disk < storage_disk_count(); ++disk) {
        for (uint32_t part = 0; part < storage_get_partition_count(disk); ++part) {
            storage_partition_info_t info;
            if (!storage_get_partition(disk, part, &info) ||
                !text_equal(info.filesystem, "FAT32")) continue;
            char source[32] = "disk";
            uint32_t length = 4U;
            uint32_t value = disk;
            char digits[10];
            uint32_t count = 0U;
            do {
                digits[count++] = (char)('0' + value % 10U);
                value /= 10U;
            } while (value != 0U);
            while (count != 0U) source[length++] = digits[--count];
            source[length++] = 'p';
            value = part + 1U;
            count = 0U;
            do {
                digits[count++] = (char)('0' + value % 10U);
                value /= 10U;
            } while (value != 0U);
            while (count != 0U) source[length++] = digits[--count];
            source[length] = '\0';
            if (vfs_mount_root("fat32", source)) {
                fat32_ready = true;
                return true;
            }
        }
    }
    fat32_ready = false;
    return false;
}

bool fat32_is_mounted(void)
{
    return fat32_ready;
}
