#include "storage.h"
#include "../include/perwix.h"

#define ATA_PRIMARY_IO   0x1f0U
#define ATA_PRIMARY_CTL  0x3f6U
#define ATA_SECONDARY_IO 0x170U
#define ATA_SECONDARY_CTL 0x376U
#define ATA_REG_DATA     0U
#define ATA_REG_ERROR    1U
#define ATA_REG_FEATURES 1U
#define ATA_REG_COUNT    2U
#define ATA_REG_LBA0     3U
#define ATA_REG_LBA1     4U
#define ATA_REG_LBA2     5U
#define ATA_REG_DRIVE    6U
#define ATA_REG_STATUS   7U
#define ATA_REG_COMMAND  7U
#define ATA_CMD_IDENTIFY 0xecU
#define ATA_CMD_READ     0x20U
#define ATA_SR_ERR       0x01U
#define ATA_SR_DRQ       0x08U
#define ATA_SR_DF        0x20U
#define ATA_SR_BSY       0x80U
#define ATA_POLL_LIMIT   1000000U
#define SECTOR_SIZE      512U

#define PART_TYPE_EMPTY  0x00U
#define PART_TYPE_EBR    0x05U
#define PART_TYPE_EBR_LBA 0x0fU
#define PART_TYPE_EBR_LINUX 0x85U
#define PART_TYPE_GPT    0xeeU

struct ata_device {
    bool present;
    uint16_t io;
    uint16_t control;
    uint8_t channel;
    uint8_t device;
    uint64_t sectors;
    char model[41];
    storage_partition_info_t partitions[STORAGE_MAX_PARTITIONS];
    uint32_t partition_count;
    bool gpt;
};

static struct ata_device disks[STORAGE_MAX_DISKS];
static uint32_t disk_count;

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const uint8_t *p)
{
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

static void copy_text(char *out, const char *in, uint32_t capacity)
{
    uint32_t i = 0;
    if (capacity == 0U) {
        return;
    }
    while (in[i] != '\0' && i + 1U < capacity) {
        out[i] = in[i];
        ++i;
    }
    out[i] = '\0';
}

static bool text_at(const uint8_t *data, uint32_t offset,
                    const char *text, uint32_t length)
{
    for (uint32_t i = 0; i < length; ++i) {
        if (data[offset + i] != (uint8_t)text[i]) {
            return false;
        }
    }
    return true;
}

static void ata_delay(uint16_t control)
{
    (void)inb(control);
    (void)inb(control);
    (void)inb(control);
    (void)inb(control);
}

static bool ata_wait_not_busy(uint16_t io, uint16_t control,
                              uint8_t *final_status)
{
    for (uint32_t i = 0; i < ATA_POLL_LIMIT; ++i) {
        uint8_t status = inb((uint16_t)(io + ATA_REG_STATUS));
        if (status == 0U || status == 0xffU) {
            return false;
        }
        if ((status & ATA_SR_BSY) == 0U) {
            if (final_status != 0) {
                *final_status = status;
            }
            return (status & (ATA_SR_ERR | ATA_SR_DF)) == 0U;
        }
        if ((i & 0x3ffU) == 0U) {
            (void)inb(control);
        }
    }
    return false;
}

static bool ata_wait_drq(uint16_t io, uint16_t control)
{
    for (uint32_t i = 0; i < ATA_POLL_LIMIT; ++i) {
        uint8_t status = inb((uint16_t)(io + ATA_REG_STATUS));
        if (status == 0U || status == 0xffU ||
            (status & (ATA_SR_ERR | ATA_SR_DF)) != 0U) {
            return false;
        }
        if ((status & ATA_SR_BSY) == 0U && (status & ATA_SR_DRQ) != 0U) {
            return true;
        }
        if ((i & 0x3ffU) == 0U) {
            (void)inb(control);
        }
    }
    return false;
}

static bool ata_identify(struct ata_device *disk)
{
    uint16_t identify[256];
    uint8_t status;

    outb((uint16_t)(disk->io + ATA_REG_DRIVE),
         (uint8_t)(0xa0U | (disk->device << 4)));
    ata_delay(disk->control);
    outb((uint16_t)(disk->io + ATA_REG_COUNT), 0U);
    outb((uint16_t)(disk->io + ATA_REG_LBA0), 0U);
    outb((uint16_t)(disk->io + ATA_REG_LBA1), 0U);
    outb((uint16_t)(disk->io + ATA_REG_LBA2), 0U);
    outb((uint16_t)(disk->io + ATA_REG_COMMAND), ATA_CMD_IDENTIFY);

    status = inb((uint16_t)(disk->io + ATA_REG_STATUS));
    if (status == 0U || status == 0xffU) {
        return false;
    }
    if (!ata_wait_not_busy(disk->io, disk->control, &status)) {
        return false;
    }

    /* ATAPI devices identify with non-zero LBA1/LBA2 signatures. */
    if (inb((uint16_t)(disk->io + ATA_REG_LBA1)) != 0U ||
        inb((uint16_t)(disk->io + ATA_REG_LBA2)) != 0U ||
        !ata_wait_drq(disk->io, disk->control)) {
        return false;
    }
    for (uint32_t i = 0; i < 256U; ++i) {
        identify[i] = inw((uint16_t)(disk->io + ATA_REG_DATA));
    }
    if ((identify[49] & (1U << 9)) == 0U) {
        return false; /* This driver intentionally requires LBA support. */
    }

    disk->sectors = (uint64_t)identify[60] |
                    ((uint64_t)identify[61] << 16);
    if (disk->sectors == 0U) {
        return false;
    }
    if (disk->sectors > 0x10000000ULL) {
        disk->sectors = 0x10000000ULL; /* LBA28 addressable range. */
    }

    for (uint32_t i = 0; i < 20U; ++i) {
        uint16_t word = identify[27U + i];
        disk->model[i * 2U] = (char)(word >> 8);
        disk->model[i * 2U + 1U] = (char)word;
    }
    disk->model[40] = '\0';
    for (int32_t i = 39; i >= 0 && disk->model[i] == ' '; --i) {
        disk->model[i] = '\0';
    }
    if (disk->model[0] == '\0') {
        copy_text(disk->model, "ATA compatible disk", sizeof(disk->model));
    }
    disk->present = true;
    return true;
}

static bool ata_read_one(struct ata_device *disk, uint64_t lba,
                         uint8_t *destination)
{
    uint8_t status;
    if (!disk->present || lba >= disk->sectors || lba > 0x0fffffffULL) {
        return false;
    }
    outb((uint16_t)(disk->io + ATA_REG_DRIVE),
         (uint8_t)(0xe0U | (disk->device << 4) | ((lba >> 24) & 0x0fU)));
    ata_delay(disk->control);
    outb((uint16_t)(disk->io + ATA_REG_FEATURES), 0U);
    outb((uint16_t)(disk->io + ATA_REG_COUNT), 1U);
    outb((uint16_t)(disk->io + ATA_REG_LBA0), (uint8_t)lba);
    outb((uint16_t)(disk->io + ATA_REG_LBA1), (uint8_t)(lba >> 8));
    outb((uint16_t)(disk->io + ATA_REG_LBA2), (uint8_t)(lba >> 16));
    outb((uint16_t)(disk->io + ATA_REG_COMMAND), ATA_CMD_READ);
    if (!ata_wait_not_busy(disk->io, disk->control, &status) ||
        !ata_wait_drq(disk->io, disk->control)) {
        return false;
    }
    for (uint32_t i = 0; i < 256U; ++i) {
        uint16_t word = inw((uint16_t)(disk->io + ATA_REG_DATA));
        destination[i * 2U] = (uint8_t)word;
        destination[i * 2U + 1U] = (uint8_t)(word >> 8);
    }
    return true;
}

bool storage_read_sectors(uint32_t disk_index, uint64_t lba,
                          uint8_t sector_count, void *buffer)
{
    if (disk_index >= disk_count || sector_count == 0U || buffer == 0 ||
        lba >= disks[disk_index].sectors ||
        (uint64_t)sector_count > disks[disk_index].sectors - lba) {
        return false;
    }
    uint64_t flags = irq_save();
    uint8_t *out = (uint8_t *)buffer;
    for (uint32_t i = 0; i < sector_count; ++i) {
        if (!ata_read_one(&disks[disk_index], lba + i,
                          out + (uint64_t)i * SECTOR_SIZE)) {
            irq_restore(flags);
            return false;
        }
    }
    irq_restore(flags);
    return true;
}

static const char *mbr_type_name(uint8_t type)
{
    switch (type) {
    case 0x01: return "FAT12";
    case 0x04: case 0x06: case 0x0e: return "FAT16";
    case 0x0b: case 0x0c: return "FAT32";
    case 0x07: return "NTFS/exFAT";
    case 0x82: return "Linux swap";
    case 0x83: return "Linux";
    case 0x8e: return "Linux LVM";
    case 0xa5: return "FreeBSD";
    case 0xa6: return "OpenBSD";
    case 0xa9: return "NetBSD";
    case 0xaf: return "Apple HFS";
    case PART_TYPE_GPT: return "GPT protective";
    case PART_TYPE_EBR: case PART_TYPE_EBR_LBA:
    case PART_TYPE_EBR_LINUX: return "Extended";
    default: return "Unknown";
    }
}

static const char *gpt_type_name(const uint8_t *guid)
{
    static const uint8_t efi_guid[16] = {
        0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,
        0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b
    };
    static const uint8_t ms_basic_guid[16] = {
        0xa2,0xa0,0xd0,0xeb,0xe5,0xb9,0x33,0x44,
        0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7
    };
    static const uint8_t linux_fs_guid[16] = {
        0xaf,0x3d,0xc6,0x0f,0x83,0x84,0x72,0x47,
        0x8e,0x79,0x3d,0x69,0xd8,0x47,0x7d,0xe4
    };
    static const uint8_t linux_swap_guid[16] = {
        0x6d,0xfd,0x57,0x06,0xab,0xa4,0xc4,0x43,
        0x84,0xe5,0x09,0x33,0xc8,0x4b,0x4f,0x4f
    };
    static const uint8_t ms_reserved_guid[16] = {
        0x16,0xe3,0xc9,0xe3,0x5c,0x0b,0xb8,0x4d,
        0x81,0x7d,0xf9,0x2d,0xf0,0x02,0x15,0xae
    };
    const uint8_t *candidate[5] = { efi_guid, ms_basic_guid, linux_fs_guid,
                                    linux_swap_guid, ms_reserved_guid };
    const char *names[5] = { "EFI System", "Microsoft Basic Data",
                             "Linux filesystem", "Linux swap", "MS Reserved" };
    for (uint32_t n = 0; n < 5U; ++n) {
        bool equal = true;
        for (uint32_t i = 0; i < 16U; ++i) {
            if (guid[i] != candidate[n][i]) {
                equal = false;
                break;
            }
        }
        if (equal) {
            return names[n];
        }
    }
    for (uint32_t i = 0; i < 16U; ++i) {
        if (guid[i] != 0U) {
            return "GPT data";
        }
    }
    return "Unused";
}

static const char *probe_filesystem(uint32_t disk_index,
                                    const storage_partition_info_t *partition)
{
    uint8_t boot[SECTOR_SIZE];
    uint8_t super[SECTOR_SIZE];
    if (partition->sectors == 0U ||
        !storage_read_sectors(disk_index, partition->first_lba, 1U, boot)) {
        return "unreadable";
    }

    if (text_at(boot, 3U, "EXFAT   ", 8U)) return "exFAT";
    if (text_at(boot, 3U, "NTFS    ", 8U)) return "NTFS";
    if (le16(boot + 510U) == 0xaa55U) {
        uint16_t bytes_per_sector = le16(boot + 11U);
        uint8_t sectors_per_cluster = boot[13U];
        uint16_t reserved = le16(boot + 14U);
        uint8_t fats = boot[16U];
        uint16_t root_entries = le16(boot + 17U);
        uint32_t total = le16(boot + 19U);
        if (total == 0U) total = le32(boot + 32U);
        uint32_t fat_size = le16(boot + 22U);
        if (fat_size == 0U) fat_size = le32(boot + 36U);
        if (bytes_per_sector == 512U && sectors_per_cluster != 0U &&
            (sectors_per_cluster & (sectors_per_cluster - 1U)) == 0U &&
            reserved != 0U && fats != 0U && fat_size != 0U && total != 0U) {
            uint32_t root_sectors =
                ((uint32_t)root_entries * 32U + 511U) / 512U;
            uint64_t used = (uint64_t)reserved +
                            (uint64_t)fats * fat_size + root_sectors;
            if (total > used) {
                uint64_t clusters = (total - used) / sectors_per_cluster;
                if (clusters < 4085U) return "FAT12";
                if (clusters < 65525U) return "FAT16";
                return "FAT32";
            }
        }
    }

    /* ext2/3/4 superblock starts at byte offset 1024. */
    if (partition->sectors > 2U &&
        storage_read_sectors(disk_index, partition->first_lba + 2U,
                             1U, super) && le16(super + 56U) == 0xef53U) {
        uint32_t compat = le32(super + 92U);
        uint32_t incompat = le32(super + 96U);
        if ((incompat & 0x40U) != 0U) return "ext4";
        if ((compat & 0x04U) != 0U) return "ext3";
        return "ext2";
    }

    if (partition->sectors > 16U &&
        storage_read_sectors(disk_index, partition->first_lba + 16U,
                             1U, super) &&
        text_at(super, 1U, "CD001", 5U)) return "ISO9660";

    if (partition->sectors > 128U &&
        storage_read_sectors(disk_index, partition->first_lba + 128U,
                             1U, super)) {
        if (text_at(super, 0x40U, "_BHRfS_M", 8U)) return "Btrfs";
        if (text_at(super, 52U, "ReIsErFs", 8U) ||
            text_at(super, 52U, "ReIsEr2Fs", 9U) ||
            text_at(super, 52U, "ReIsEr3Fs", 9U)) return "ReiserFS";
    }

    if (text_at(boot, 0U, "XFSB", 4U)) return "XFS";
    if (text_at(boot, 0U, "hsqs", 4U)) return "SquashFS";
    if (text_at(boot, 0U, "LUKS\xba\xbe", 6U)) return "LUKS";
    if (partition->sectors > 1U &&
        storage_read_sectors(disk_index, partition->first_lba + 1U,
                             1U, super) &&
        text_at(super, 0U, "LABELONE", 8U)) return "LVM2";
    if (partition->sectors > 7U &&
        storage_read_sectors(disk_index, partition->first_lba + 7U,
                             1U, super) &&
        text_at(super, 502U, "SWAPSPACE2", 10U)) return "Linux swap";
    if (partition->sectors > 2U &&
        storage_read_sectors(disk_index, partition->first_lba + 2U,
                             1U, super)) {
        if (le32(super) == 0xf2f52010U) return "F2FS";
        if (le16(super) == 0x482bU || le16(super) == 0x4858U) return "HFS+";
    }
    if (partition->sectors > 18U &&
        storage_read_sectors(disk_index, partition->first_lba + 18U,
                             1U, super) && le32(super + 348U) == 0x00011954U) {
        return "UFS";
    }
    if (partition->sectors > 64U &&
        storage_read_sectors(disk_index, partition->first_lba + 64U,
                             1U, super)) {
        if (text_at(super, 0U, "JFS1", 4U)) return "JFS";
        if (text_at(super, 32U, "NXSB", 4U)) return "APFS";
    }
    return "unknown";
}

static void add_partition(struct ata_device *disk, uint64_t first,
                          uint64_t count, uint8_t mbr_type, bool bootable,
                          const char *type_name)
{
    if (disk->partition_count >= STORAGE_MAX_PARTITIONS || count == 0U ||
        first >= disk->sectors || count > disk->sectors - first) {
        return;
    }
    storage_partition_info_t *part =
        &disk->partitions[disk->partition_count++];
    part->first_lba = first;
    part->sectors = count;
    part->mbr_type = mbr_type;
    part->bootable = bootable;
    part->type_name = type_name;
    part->filesystem = "probing";
}

static void scan_gpt(uint32_t disk_index, struct ata_device *disk)
{
    uint8_t header[SECTOR_SIZE];
    if (!ata_read_one(disk, 1U, header) || !text_at(header, 0U, "EFI PART", 8U)) {
        return;
    }
    uint32_t header_size = le32(header + 12U);
    uint64_t entries_lba = le64(header + 72U);
    uint32_t entry_count = le32(header + 80U);
    uint32_t entry_size = le32(header + 84U);
    if (header_size < 92U || header_size > SECTOR_SIZE ||
        entry_count == 0U || entry_size != 128U ||
        entries_lba >= disk->sectors) {
        return;
    }
    if (entry_count > 128U) entry_count = 128U;
    uint64_t current_sector = UINT64_MAX;
    uint8_t entries_sector[SECTOR_SIZE];
    for (uint32_t i = 0; i < entry_count &&
         disk->partition_count < STORAGE_MAX_PARTITIONS; ++i) {
        uint64_t lba = entries_lba + i / 4U;
        if (lba >= disk->sectors) break;
        if (lba != current_sector) {
            if (!ata_read_one(disk, lba, entries_sector)) break;
            current_sector = lba;
        }
        const uint8_t *entry = entries_sector + (i % 4U) * 128U;
        const char *type_name = gpt_type_name(entry);
        if (type_name[0] == 'U' && type_name[1] == 'n') continue;
        uint64_t first = le64(entry + 32U);
        uint64_t last = le64(entry + 40U);
        if (last < first) continue;
        add_partition(disk, first, last - first + 1U, 0U,
                      (le64(entry + 48U) & 4U) != 0U, type_name);
    }
    disk->gpt = true;
    for (uint32_t i = 0; i < disk->partition_count; ++i) {
        disk->partitions[i].filesystem =
            probe_filesystem(disk_index, &disk->partitions[i]);
    }
}

static void scan_partitions(uint32_t disk_index, struct ata_device *disk)
{
    uint8_t mbr[SECTOR_SIZE];
    disk->partition_count = 0U;
    disk->gpt = false;
    if (!ata_read_one(disk, 0U, mbr) || le16(mbr + 510U) != 0xaa55U) {
        return;
    }
    for (uint32_t i = 0; i < 4U; ++i) {
        const uint8_t *entry = mbr + 446U + i * 16U;
        uint8_t type = entry[4U];
        uint32_t first = le32(entry + 8U);
        uint32_t count = le32(entry + 12U);
        if (type == PART_TYPE_EMPTY || count == 0U) continue;
        if (type == PART_TYPE_GPT) {
            scan_gpt(disk_index, disk);
            return;
        }
        add_partition(disk, first, count, type,
                      (entry[0] & 0x80U) != 0U, mbr_type_name(type));
    }
    for (uint32_t i = 0; i < disk->partition_count; ++i) {
        disk->partitions[i].filesystem =
            probe_filesystem(disk_index, &disk->partitions[i]);
    }
}

void storage_rescan(void)
{
    disk_count = 0U;
    for (uint32_t channel = 0; channel < 2U; ++channel) {
        for (uint32_t device = 0; device < 2U; ++device) {
            if (disk_count >= STORAGE_MAX_DISKS) return;
            struct ata_device *disk = &disks[disk_count];
            disk->present = false;
            disk->io = channel == 0U ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
            disk->control = channel == 0U ? ATA_PRIMARY_CTL : ATA_SECONDARY_CTL;
            disk->channel = (uint8_t)channel;
            disk->device = (uint8_t)device;
            disk->partition_count = 0U;
            disk->gpt = false;
            if (ata_identify(disk)) {
                ++disk_count;
                scan_partitions(disk_count - 1U, disk);
            }
        }
    }
}

void storage_init(void)
{
    storage_rescan();
}

uint32_t storage_disk_count(void)
{
    return disk_count;
}

bool storage_get_disk(uint32_t index, storage_disk_info_t *result)
{
    if (index >= disk_count || result == 0) return false;
    const struct ata_device *disk = &disks[index];
    result->present = disk->present;
    result->channel = disk->channel;
    result->device = disk->device;
    result->gpt = disk->gpt;
    result->sectors = disk->sectors;
    result->partition_count = disk->partition_count;
    copy_text(result->model, disk->model, sizeof(result->model));
    return true;
}

uint32_t storage_get_partition_count(uint32_t disk_index)
{
    return disk_index < disk_count ? disks[disk_index].partition_count : 0U;
}

bool storage_get_partition(uint32_t disk_index, uint32_t partition_index,
                           storage_partition_info_t *result)
{
    if (disk_index >= disk_count || result == 0 ||
        partition_index >= disks[disk_index].partition_count) return false;
    *result = disks[disk_index].partitions[partition_index];
    return true;
}
