#include "module.h"
#include "perwix.h"
#include "storage.h"

static int32_t selected_disk = -1;

static bool text_equal(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static bool text_starts(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (*text++ != *prefix++) return false;
    }
    return true;
}

static void print_size_mib(uint64_t sectors)
{
    uint64_t mib = sectors / 2048U;
    console_write_u64(mib);
    console_write(" MiB");
}

static bool parse_number(const char *text, uint32_t *value)
{
    uint32_t result = 0;
    if (*text == '\0') return false;
    while (*text >= '0' && *text <= '9') {
        uint32_t digit = (uint32_t)(*text - '0');
        if (result > (0xffffffffU - digit) / 10U) return false;
        result = result * 10U + digit;
        ++text;
    }
    if (*text != '\0') return false;
    *value = result;
    return true;
}

static void list_disks(void)
{
    uint32_t count = storage_disk_count();
    console_write("Disk  Model                                  Size       Layout\n");
    if (count == 0U) {
        console_write("No ATA/IDE disk detected. AHCI/SATA, NVMe and USB storage are not yet supported.\n");
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        storage_disk_info_t disk;
        if (!storage_get_disk(i, &disk)) continue;
        console_write("  ");
        console_write_u64(i);
        console_write("    ");
        console_write(disk.model);
        console_write("  ");
        print_size_mib(disk.sectors);
        console_write("  ");
        console_write(disk.gpt ? "GPT" : "MBR/unknown");
        console_write("\n");
    }
}

static bool selected_info(storage_disk_info_t *disk)
{
    if (selected_disk < 0 ||
        !storage_get_disk((uint32_t)selected_disk, disk)) {
        console_write("No disk selected. Use: select disk N\n");
        return false;
    }
    return true;
}

static void detail_disk(void)
{
    storage_disk_info_t disk;
    if (!selected_info(&disk)) return;
    console_write("Disk ");
    console_write_u64((uint64_t)selected_disk);
    console_write(": ");
    console_write(disk.model);
    console_write("\n  Bus: ATA/IDE PIO, ");
    console_write(disk.channel == 0U ? "primary " : "secondary ");
    console_write(disk.device == 0U ? "master\n" : "slave\n");
    console_write("  Sectors: ");
    console_write_u64(disk.sectors);
    console_write(" (512 bytes each)\n  Partition table: ");
    console_write(disk.gpt ? "GPT\n" : "MBR or unknown\n");
    console_write("  Partitions: ");
    console_write_u64(disk.partition_count);
    console_write("\n");
}

static void list_partitions(void)
{
    storage_disk_info_t disk;
    if (!selected_info(&disk)) return;
    console_write("Partition  Type                  Filesystem   Start LBA       Size\n");
    uint32_t count = storage_get_partition_count((uint32_t)selected_disk);
    if (count == 0U) {
        console_write("No supported primary partitions found.\n");
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        storage_partition_info_t part;
        if (!storage_get_partition((uint32_t)selected_disk, i, &part)) continue;
        console_write("  ");
        console_write_u64(i + 1U);
        console_write("         ");
        console_write(part.type_name);
        console_write("  ");
        console_write(part.filesystem);
        console_write("  ");
        console_write_u64(part.first_lba);
        console_write("  ");
        print_size_mib(part.sectors);
        if (part.bootable) console_write("  *boot*");
        console_write("\n");
    }
}

static void list_volumes(void)
{
    bool found = false;
    console_write("Volume  Disk  Partition  Filesystem   Size       State\n");
    for (uint32_t d = 0; d < storage_disk_count(); ++d) {
        uint32_t count = storage_get_partition_count(d);
        for (uint32_t p = 0; p < count; ++p) {
            storage_partition_info_t part;
            if (!storage_get_partition(d, p, &part)) continue;
            found = true;
            console_write("  ");
            console_write_u64(d * STORAGE_MAX_PARTITIONS + p);
            console_write("      ");
            console_write_u64(d);
            console_write("      ");
            console_write_u64(p + 1U);
            console_write("          ");
            console_write(part.filesystem);
            console_write("  ");
            print_size_mib(part.sectors);
            console_write("  ");
            console_write(text_equal(part.filesystem, "unknown") ?
                          "unrecognized\n" : "detected\n");
        }
    }
    if (!found) console_write("No partitioned volumes detected.\n");
}

static void diskpart_help(void)
{
    console_write("Perwix DiskPart - simple read-only disk manager\n");
    console_write("  list disk          show detected ATA disks\n");
    console_write("  select disk N      select a disk by number\n");
    console_write("  detail disk        show selected disk information\n");
    console_write("  list partition     show selected disk partitions\n");
    console_write("  list volume        show detected filesystem signatures\n");
    console_write("  rescan             probe ATA disks again\n");
    console_write("  help               show this help\n");
    console_write("This build is read-only: no partition or filesystem data is changed.\n");
}

static void diskpart_execute(const char *arguments)
{
    char args[96];
    uint32_t n = 0;
    while (arguments[n] != '\0' && n + 1U < sizeof(args)) {
        args[n] = arguments[n];
        ++n;
    }
    args[n] = '\0';
    char *p = args;
    while (*p == ' ' || *p == '\t') ++p;

    if (*p == '\0' || text_equal(p, "help") || text_equal(p, "/?")) {
        diskpart_help();
    } else if (text_equal(p, "list disk")) {
        list_disks();
    } else if (text_equal(p, "list volume")) {
        list_volumes();
    } else if (text_equal(p, "list partition")) {
        list_partitions();
    } else if (text_starts(p, "select disk ")) {
        uint32_t index;
        if (!parse_number(p + 12, &index) || index >= storage_disk_count()) {
            console_write("Invalid disk number. Run 'list disk'.\n");
        } else {
            selected_disk = (int32_t)index;
            console_write("Disk ");
            console_write_u64(index);
            console_write(" is now the selected disk.\n");
        }
    } else if (text_equal(p, "detail disk")) {
        detail_disk();
    } else if (text_equal(p, "rescan")) {
        storage_rescan();
        selected_disk = -1;
        console_write("Storage scan complete. Disks found: ");
        console_write_u64(storage_disk_count());
        console_write("\n");
    } else {
        console_write("Unknown DiskPart command. Type 'diskpart help'.\n");
    }
}

bool perwix_module_init(void)
{
    return perwix_command_register("diskpart",
                                   "simple read-only disk and partition manager",
                                   diskpart_execute);
}
