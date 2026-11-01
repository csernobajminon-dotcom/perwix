#include "perwix.h"
#include "module.h"
#include "netdev.h"
#include "../storage/storage.h"

#define MB2_TAG_END 0U
#define MB2_TAG_MODULE 3U
#define ELFCLASS64 2U
#define ELFDATA2LSB 1U
#define ET_REL 1U
#define EM_X86_64 62U
#define SHT_PROGBITS 1U
#define SHT_SYMTAB 2U
#define SHT_STRTAB 3U
#define SHT_RELA 4U
#define SHT_NOBITS 8U
#define SHF_ALLOC 2ULL
#define SHN_UNDEF 0U
#define SHN_ABS 0xfff1U
#define R_X86_64_NONE 0U
#define R_X86_64_64 1U
#define R_X86_64_PC32 2U
#define R_X86_64_PLT32 4U
#define R_X86_64_32 10U
#define R_X86_64_32S 11U
#define R_X86_64_PC64 24U
#define ELF_MAX_SECTIONS 256U
#define ELF_MAX_MODULES 32U
#define ELF_MAX_COMMANDS 32U
#define ELF_MAX_MODULE_BYTES (8U * 1024U * 1024U)
#define PAGE_SIZE 4096U

struct mb2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct mb2_module_tag {
    uint32_t type;
    uint32_t size;
    uint32_t start;
    uint32_t end;
    char command_line[];
} __attribute__((packed));

struct elf64_header {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

struct elf64_section {
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t addr;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t addralign;
    uint64_t entsize;
} __attribute__((packed));

struct elf64_symbol {
    uint32_t name;
    uint8_t info;
    uint8_t other;
    uint16_t section_index;
    uint64_t value;
    uint64_t size;
} __attribute__((packed));

struct elf64_rela {
    uint64_t offset;
    uint64_t info;
    int64_t addend;
} __attribute__((packed));

typedef struct {
    const char *name;
    uint64_t address;
} kernel_export_t;

typedef struct {
    const char *name;
    const char *description;
    perwix_command_fn execute;
} command_entry_t;

static command_entry_t commands[ELF_MAX_COMMANDS];
static uint32_t command_count;
static char loaded_modules[ELF_MAX_MODULES][40];
static uint32_t loaded_module_count;

/* Only explicitly exported kernel interfaces can be called by ELF modules. */
static const kernel_export_t kernel_exports[] = {
    { "console_write", (uint64_t)(uintptr_t)console_write },
    { "console_write_u64", (uint64_t)(uintptr_t)console_write_u64 },
    { "console_write_hex", (uint64_t)(uintptr_t)console_write_hex },
    { "pmm_bootstrap_pages", (uint64_t)(uintptr_t)pmm_bootstrap_pages },
    { "system_ticks", (uint64_t)(uintptr_t)&system_ticks },
    { "inb", (uint64_t)(uintptr_t)inb },
    { "inw", (uint64_t)(uintptr_t)inw },
    { "inl", (uint64_t)(uintptr_t)inl },
    { "outb", (uint64_t)(uintptr_t)outb },
    { "outw", (uint64_t)(uintptr_t)outw },
    { "outl", (uint64_t)(uintptr_t)outl },
    { "pci_config_read32", (uint64_t)(uintptr_t)pci_config_read32 },
    { "nic_alloc_dma", (uint64_t)(uintptr_t)nic_alloc_dma },
    { "nic_delay", (uint64_t)(uintptr_t)nic_delay },
    { "nic_pci_enable", (uint64_t)(uintptr_t)nic_pci_enable },
    { "perwix_command_register", (uint64_t)(uintptr_t)perwix_command_register },
    { "net_register_nic_driver", (uint64_t)(uintptr_t)net_register_nic_driver },
    { "storage_rescan", (uint64_t)(uintptr_t)storage_rescan },
    { "storage_disk_count", (uint64_t)(uintptr_t)storage_disk_count },
    { "storage_get_disk", (uint64_t)(uintptr_t)storage_get_disk },
    { "storage_get_partition_count", (uint64_t)(uintptr_t)storage_get_partition_count },
    { "storage_get_partition", (uint64_t)(uintptr_t)storage_get_partition },
    { "storage_read_sectors", (uint64_t)(uintptr_t)storage_read_sectors },
};

static bool bytes_equal(const uint8_t *left, const uint8_t *right,
                        uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }
    return true;
}

static void bytes_copy(void *destination, const void *source, uint64_t count)
{
    uint8_t *out = (uint8_t *)destination;
    const uint8_t *in = (const uint8_t *)source;
    for (uint64_t i = 0; i < count; ++i) {
        out[i] = in[i];
    }
}

static bool text_equal(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return false;
    }
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static bool range_valid(uint64_t offset, uint64_t length, uint64_t total)
{
    return offset <= total && length <= total - offset;
}

static bool bounded_string(const char *table, uint64_t table_size,
                           uint32_t offset, const char **result)
{
    if (offset >= table_size) {
        return false;
    }
    for (uint64_t i = offset; i < table_size; ++i) {
        if (table[i] == '\0') {
            *result = table + offset;
            return true;
        }
    }
    return false;
}

static uint64_t resolve_kernel_symbol(const char *name)
{
    for (uint32_t i = 0;
         i < sizeof(kernel_exports) / sizeof(kernel_exports[0]); ++i) {
        if (text_equal(name, kernel_exports[i].name)) {
            return kernel_exports[i].address;
        }
    }
    return 0;
}

static bool resolve_symbol(const uint8_t *image, uint64_t image_size,
                           const struct elf64_section *sections,
                           uint16_t section_count, const void **section_bases,
                           const struct elf64_symbol *symbols,
                           uint32_t symbol_count, const char *strings,
                           uint64_t strings_size, uint32_t symbol_index,
                           uint64_t *address)
{
    if (symbol_index >= symbol_count) {
        return false;
    }
    const struct elf64_symbol *symbol = &symbols[symbol_index];
    if (symbol->section_index == SHN_UNDEF) {
        const char *name;
        if (!bounded_string(strings, strings_size, symbol->name, &name)) {
            return false;
        }
        uint64_t resolved = resolve_kernel_symbol(name);
        if (resolved == 0 && (symbol->info >> 4) == 2U) {
            *address = 0; /* Undefined weak symbol. */
            return true;
        }
        if (resolved == 0) {
            (void)image;
            (void)image_size;
            return false;
        }
        *address = resolved;
        return true;
    }
    if (symbol->section_index == SHN_ABS) {
        *address = symbol->value;
        return true;
    }
    if (symbol->section_index >= section_count ||
        section_bases[symbol->section_index] == NULL ||
        symbol->value > sections[symbol->section_index].size) {
        return false;
    }
    *address = (uint64_t)(uintptr_t)section_bases[symbol->section_index] +
               symbol->value;
    return true;
}

static bool apply_relocations(const uint8_t *image, uint64_t image_size,
                              const struct elf64_section *sections,
                              uint16_t section_count,
                              const void **section_bases)
{
    for (uint16_t r = 0; r < section_count; ++r) {
        const struct elf64_section *rel_section = &sections[r];
        if (rel_section->type != SHT_RELA) {
            continue;
        }
        if (rel_section->info >= section_count ||
            rel_section->link >= section_count ||
            rel_section->entsize != sizeof(struct elf64_rela) ||
            rel_section->size % sizeof(struct elf64_rela) != 0 ||
            !range_valid(rel_section->offset, rel_section->size, image_size)) {
            return false;
        }
        const struct elf64_section *target = &sections[rel_section->info];
        const struct elf64_section *symtab = &sections[rel_section->link];
        if ((target->flags & SHF_ALLOC) == 0) {
            continue; /* Debug/non-loaded sections need no runtime relocation. */
        }
        if (section_bases[rel_section->info] == NULL ||
            symtab->type != SHT_SYMTAB ||
            symtab->entsize != sizeof(struct elf64_symbol) ||
            symtab->size % sizeof(struct elf64_symbol) != 0 ||
            symtab->link >= section_count) {
            return false;
        }
        const struct elf64_section *strtab = &sections[symtab->link];
        if (strtab->type != SHT_STRTAB ||
            !range_valid(symtab->offset, symtab->size, image_size) ||
            !range_valid(strtab->offset, strtab->size, image_size)) {
            return false;
        }
        const struct elf64_symbol *symbols =
            (const struct elf64_symbol *)(const void *)(image + symtab->offset);
        uint32_t symbol_count = (uint32_t)(symtab->size / sizeof(*symbols));
        const char *strings = (const char *)(const void *)(image + strtab->offset);
        uint64_t strings_size = strtab->size;
        const struct elf64_rela *entries =
            (const struct elf64_rela *)(const void *)(image + rel_section->offset);
        uint64_t entry_count = rel_section->size / sizeof(*entries);
        uint8_t *target_base = (uint8_t *)(uintptr_t)section_bases[rel_section->info];

        for (uint64_t i = 0; i < entry_count; ++i) {
            uint32_t type = (uint32_t)(entries[i].info & 0xffffffffU);
            uint32_t symbol_index = (uint32_t)(entries[i].info >> 32);
            uint64_t width = (type == R_X86_64_64 || type == R_X86_64_PC64) ? 8U :
                             (type == R_X86_64_NONE ? 0U : 4U);
            if (type == R_X86_64_NONE) {
                continue;
            }
            if (type != R_X86_64_64 && type != R_X86_64_PC32 &&
                type != R_X86_64_PLT32 && type != R_X86_64_32 &&
                type != R_X86_64_32S && type != R_X86_64_PC64) {
                return false;
            }
            if (!range_valid(entries[i].offset, width, target->size)) {
                return false;
            }
            uint64_t symbol_address;
            if (!resolve_symbol(image, image_size, sections, section_count,
                                section_bases, symbols, symbol_count, strings,
                                strings_size, symbol_index, &symbol_address)) {
                return false;
            }
            uint64_t place = (uint64_t)(uintptr_t)target_base + entries[i].offset;
            int64_t addend = entries[i].addend;
            uint8_t *where = target_base + entries[i].offset;
            if (type == R_X86_64_64) {
                uint64_t value = symbol_address + (uint64_t)addend;
                bytes_copy(where, &value, sizeof(value));
            } else if (type == R_X86_64_PC64) {
                int64_t value = (int64_t)symbol_address + addend - (int64_t)place;
                bytes_copy(where, &value, sizeof(value));
            } else if (type == R_X86_64_PC32 || type == R_X86_64_PLT32) {
                int64_t value = (int64_t)symbol_address + addend - (int64_t)place;
                if (value < -2147483648LL || value > 2147483647LL) {
                    return false;
                }
                int32_t narrowed = (int32_t)value;
                bytes_copy(where, &narrowed, sizeof(narrowed));
            } else if (type == R_X86_64_32) {
                uint64_t value = symbol_address + (uint64_t)addend;
                if (value > 0xffffffffULL) {
                    return false;
                }
                uint32_t narrowed = (uint32_t)value;
                bytes_copy(where, &narrowed, sizeof(narrowed));
            } else if (type == R_X86_64_32S) {
                int64_t value = (int64_t)symbol_address + addend;
                if (value < -2147483648LL || value > 2147483647LL) {
                    return false;
                }
                int32_t narrowed = (int32_t)value;
                bytes_copy(where, &narrowed, sizeof(narrowed));
            }
        }
    }
    return true;
}

static bool load_elf_module(const uint8_t *image, uint64_t image_size)
{
    if (image == NULL || image_size < sizeof(struct elf64_header) ||
        image_size > ELF_MAX_MODULE_BYTES) {
        return false;
    }
    const struct elf64_header *header =
        (const struct elf64_header *)(const void *)image;
    if (!bytes_equal(header->ident, (const uint8_t *)"\x7f" "ELF", 4) ||
        header->ident[4] != ELFCLASS64 || header->ident[5] != ELFDATA2LSB ||
        header->type != ET_REL || header->machine != EM_X86_64 ||
        header->version != 1U || header->shentsize != sizeof(struct elf64_section) ||
        header->shnum == 0 || header->shnum > ELF_MAX_SECTIONS ||
        !range_valid(header->shoff,
                     (uint64_t)header->shnum * sizeof(struct elf64_section),
                     image_size)) {
        return false;
    }
    const struct elf64_section *sections =
        (const struct elf64_section *)(const void *)(image + header->shoff);
    const void *section_bases[ELF_MAX_SECTIONS];
    for (uint32_t i = 0; i < ELF_MAX_SECTIONS; ++i) {
        section_bases[i] = NULL;
    }

    const struct elf64_section *symtab = NULL;
    uint32_t symtab_index = 0;
    for (uint16_t i = 0; i < header->shnum; ++i) {
        const struct elf64_section *section = &sections[i];
        if (section->type != SHT_NOBITS &&
            !range_valid(section->offset, section->size, image_size)) {
            return false;
        }
        if ((section->flags & SHF_ALLOC) == 0 || section->size == 0) {
            continue;
        }
        uint64_t alignment = section->addralign == 0 ? 1U : section->addralign;
        if (alignment > PAGE_SIZE || (alignment & (alignment - 1U)) != 0) {
            return false;
        }
        uint32_t pages = (uint32_t)((section->size + PAGE_SIZE - 1U) / PAGE_SIZE);
        void *memory = pmm_alloc_pages(pages);
        if (memory == NULL) {
            return false;
        }
        section_bases[i] = memory;
        if (section->type == SHT_PROGBITS) {
            bytes_copy(memory, image + section->offset, section->size);
        } else if (section->type != SHT_NOBITS) {
            return false;
        }
    }
    for (uint16_t i = 0; i < header->shnum; ++i) {
        if (sections[i].type == SHT_SYMTAB) {
            symtab = &sections[i];
            symtab_index = i;
            break;
        }
    }
    if (symtab == NULL || symtab->entsize != sizeof(struct elf64_symbol) ||
        symtab->size % sizeof(struct elf64_symbol) != 0 ||
        symtab->link >= header->shnum ||
        sections[symtab->link].type != SHT_STRTAB ||
        !range_valid(symtab->offset, symtab->size, image_size) ||
        !range_valid(sections[symtab->link].offset,
                     sections[symtab->link].size, image_size)) {
        return false;
    }
    if (!apply_relocations(image, image_size, sections, header->shnum,
                           section_bases)) {
        return false;
    }

    const struct elf64_symbol *symbols =
        (const struct elf64_symbol *)(const void *)(image + symtab->offset);
    uint32_t symbol_count = (uint32_t)(symtab->size / sizeof(*symbols));
    const struct elf64_section *strtab = &sections[symtab->link];
    const char *strings = (const char *)(const void *)(image + strtab->offset);
    uint64_t strings_size = strtab->size;
    for (uint32_t i = 0; i < symbol_count; ++i) {
        const char *name;
        if (!bounded_string(strings, strings_size, symbols[i].name, &name)) {
            continue;
        }
        if (text_equal(name, "perwix_module_init")) {
            uint64_t entry_address;
            if (!resolve_symbol(image, image_size, sections, header->shnum,
                                section_bases, symbols, symbol_count, strings,
                                strings_size, i, &entry_address) ||
                entry_address == 0) {
                return false;
            }
            bool (*entry)(void) = (bool (*)(void))(uintptr_t)entry_address;
            (void)symtab_index;
            return entry();
        }
    }
    return false;
}

static void module_label(const char *command_line, char *name,
                         uint32_t capacity)
{
    const char *first = command_line;
    const char *first_end = first;
    while (*first_end != '\0' && *first_end != ' ' && *first_end != '\t') {
        ++first_end;
    }
    const char *label = first_end;
    while (*label == ' ' || *label == '\t') {
        ++label;
    }
    if (*label == '\0') {
        label = first;
        for (const char *cursor = first; cursor < first_end; ++cursor) {
            if (*cursor == '/') {
                label = cursor + 1;
            }
        }
    }

    uint32_t length = 0;
    while (label[length] != '\0' && label[length] != ' ' &&
           label[length] != '\t' && length + 1U < capacity) {
        name[length] = label[length];
        ++length;
    }
    name[length] = '\0';
    for (uint32_t i = 0; i < length; ++i) {
        if (name[i] == '.') {
            name[i] = '\0';
            break;
        }
    }
    if (name[0] == '\0') {
        const char fallback[] = "unnamed";
        bytes_copy(name, fallback, sizeof(fallback));
    }
}

bool perwix_command_register(const char *name, const char *description,
                             perwix_command_fn execute)
{
    if (name == NULL || description == NULL || execute == NULL ||
        name[0] == '\0' || command_count == ELF_MAX_COMMANDS) {
        return false;
    }
    for (uint32_t i = 0; i < command_count; ++i) {
        if (text_equal(commands[i].name, name)) {
            return false;
        }
    }
    commands[command_count].name = name;
    commands[command_count].description = description;
    commands[command_count].execute = execute;
    ++command_count;
    return true;
}

bool perwix_command_execute(const char *name, const char *arguments)
{
    for (uint32_t i = 0; i < command_count; ++i) {
        if (text_equal(commands[i].name, name)) {
            commands[i].execute(arguments != NULL ? arguments : "");
            return true;
        }
    }
    return false;
}

void perwix_command_list(void)
{
    console_write("ELF commands:\n");
    if (command_count == 0) {
        console_write("  (none loaded)\n");
        return;
    }
    for (uint32_t i = 0; i < command_count; ++i) {
        console_write("  ");
        console_write(commands[i].name);
        console_write(" - ");
        console_write(commands[i].description);
        console_write("\n");
    }
}

uint32_t perwix_module_count(void)
{
    return loaded_module_count;
}

const char *perwix_module_name(uint32_t index)
{
    return index < loaded_module_count ? loaded_modules[index] : "invalid";
}

uint32_t perwix_modules_load(uint64_t multiboot_info_address)
{
    if (multiboot_info_address == 0) {
        return 0;
    }
    const uint8_t *info = (const uint8_t *)(uintptr_t)multiboot_info_address;
    uint32_t total_size = *(const uint32_t *)(const void *)info;
    if (total_size < 16U || total_size > 16U * 1024U * 1024U) {
        return 0;
    }
    uint32_t offset = 8U;
    while (offset <= total_size - 8U && loaded_module_count < ELF_MAX_MODULES) {
        const struct mb2_tag *tag =
            (const struct mb2_tag *)(const void *)(info + offset);
        if (tag->size < sizeof(*tag) || tag->size > total_size - offset) {
            break;
        }
        if (tag->type == MB2_TAG_END) {
            break;
        }
        if (tag->type == MB2_TAG_MODULE && tag->size >= 16U) {
            const struct mb2_module_tag *module =
                (const struct mb2_module_tag *)(const void *)tag;
            uint64_t start = module->start;
            uint64_t end = module->end;
            if (end > start && end <= 0x100000000ULL) {
                char name[40];
                module_label(module->command_line, name, sizeof(name));
                if (load_elf_module((const uint8_t *)(uintptr_t)start,
                                    end - start)) {
                    uint32_t n = 0;
                    while (name[n] != '\0' && n + 1U < sizeof(loaded_modules[0])) {
                        loaded_modules[loaded_module_count][n] = name[n];
                        ++n;
                    }
                    loaded_modules[loaded_module_count][n] = '\0';
                    ++loaded_module_count;
                } else {
                    console_write("[module] rejected ELF module: ");
                    console_write(name);
                    console_write("\n");
                }
            }
        }
        uint32_t aligned = (tag->size + 7U) & ~7U;
        if (aligned > total_size - offset) {
            break;
        }
        offset += aligned;
    }
    return loaded_module_count;
}
