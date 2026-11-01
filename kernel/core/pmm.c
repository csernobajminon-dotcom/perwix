#include "perwix.h"

#define MULTIBOOT2_TAG_END 0
#define MULTIBOOT2_TAG_MMAP 6
#define PAGE_SIZE 4096ULL
#define ADDRESS_LIMIT 0x100000000ULL

struct multiboot2_info {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed));

struct multiboot2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct multiboot2_mmap_tag {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} __attribute__((packed));

struct multiboot2_mmap_entry {
    uint64_t address;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

extern char __kernel_end;

static uint64_t next_page;
static uint64_t region_end;
static uint64_t region_pages;

static uint64_t align_up(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

void pmm_init(uint64_t multiboot_info_address)
{
    next_page = 0;
    region_end = 0;
    region_pages = 0;

    const struct multiboot2_info *info =
        (const struct multiboot2_info *)(uintptr_t)multiboot_info_address;
    if (info == NULL || info->total_size < sizeof(*info)) {
        return;
    }

    uint64_t reserved_end = multiboot_info_address + info->total_size;
    if (reserved_end < multiboot_info_address) {
        return;
    }
    if (reserved_end < (uint64_t)(uintptr_t)&__kernel_end) {
        reserved_end = (uint64_t)(uintptr_t)&__kernel_end;
    }
    reserved_end = align_up(reserved_end + PAGE_SIZE, PAGE_SIZE);

    const uint8_t *cursor =
        (const uint8_t *)(uintptr_t)multiboot_info_address + 8;
    const uint8_t *limit =
        (const uint8_t *)(uintptr_t)multiboot_info_address + info->total_size;

    while (cursor + sizeof(struct multiboot2_tag) <= limit) {
        const struct multiboot2_tag *tag =
            (const struct multiboot2_tag *)(const void *)cursor;
        if (tag->size < sizeof(*tag) || cursor + tag->size > limit) {
            break;
        }
        if (tag->type == MULTIBOOT2_TAG_END) {
            break;
        }
        if (tag->type == MULTIBOOT2_TAG_MMAP &&
            tag->size >= sizeof(struct multiboot2_mmap_tag)) {
            const struct multiboot2_mmap_tag *mmap =
                (const struct multiboot2_mmap_tag *)(const void *)tag;
            if (mmap->entry_size >= sizeof(struct multiboot2_mmap_entry)) {
                const uint8_t *entry_cursor = cursor + sizeof(*mmap);
                const uint8_t *entry_limit = cursor + tag->size;
                while (entry_cursor + mmap->entry_size <= entry_limit) {
                    const struct multiboot2_mmap_entry *entry =
                        (const struct multiboot2_mmap_entry *)(const void *)entry_cursor;
                    uint64_t end = entry->address + entry->length;
                    if (entry->type == 1 && end > entry->address &&
                        entry->address < ADDRESS_LIMIT) {
                        uint64_t start = entry->address;
                        if (start < reserved_end) {
                            start = reserved_end;
                        }
                        if (end > ADDRESS_LIMIT) {
                            end = ADDRESS_LIMIT;
                        }
                        start = align_up(start, PAGE_SIZE);
                        end &= ~(PAGE_SIZE - 1);
                        if (end > start) {
                            uint64_t pages = (end - start) / PAGE_SIZE;
                            if (pages > region_pages) {
                                next_page = start;
                                region_end = end;
                                region_pages = pages;
                            }
                        }
                    }
                    entry_cursor += mmap->entry_size;
                }
            }
        }
        cursor += (tag->size + 7U) & ~7U;
    }

    if (region_pages == 0) {
        return;
    }
}

uint64_t pmm_bootstrap_pages(void)
{
    return region_pages;
}

void *pmm_alloc_page(void)
{
    if (next_page == 0 || next_page + PAGE_SIZE > region_end) {
        return NULL;
    }
    uint64_t allocated = next_page;
    next_page += PAGE_SIZE;
    if (region_pages != 0) {
        --region_pages;
    }
    return (void *)(uintptr_t)allocated;
}

/* Allocate `count` physically contiguous, zeroed pages (bump allocator). */
void *pmm_alloc_pages(uint32_t count)
{
    uint64_t bytes = (uint64_t)count * PAGE_SIZE;
    if (count == 0 || next_page == 0 || next_page + bytes > region_end) {
        return NULL;
    }
    uint8_t *memory = (uint8_t *)(uintptr_t)next_page;
    next_page += bytes;
    region_pages = region_pages >= count ? region_pages - count : 0;
    for (uint64_t i = 0; i < bytes; ++i) {
        memory[i] = 0;
    }
    return memory;
}
