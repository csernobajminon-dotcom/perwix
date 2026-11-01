#include "module.h"
#include "perwix.h"

static void command_meminfo(const char *arguments)
{
    uint64_t pages = pmm_bootstrap_pages();
    (void)arguments;
    console_write("Available bootstrap pages: ");
    console_write_u64(pages);
    console_write(" (4 KiB each)\n");
    console_write("Approximate available memory: ");
    console_write_u64((pages * 4U) / 1024U);
    console_write(" MiB\n");
}

bool perwix_module_init(void)
{
    return perwix_command_register("meminfo", "show memory allocator statistics", command_meminfo);
}
