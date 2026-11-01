#include "module.h"
#include "perwix.h"

static void command_uname(const char *arguments)
{
    (void)arguments;
    console_write("Perwix 1.1 | x86_64 | ELF modules | Multiboot2\n");
}

bool perwix_module_init(void)
{
    return perwix_command_register("uname", "show system and architecture", command_uname);
}
