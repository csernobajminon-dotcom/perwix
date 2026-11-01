#include "module.h"
#include "perwix.h"

static void command_hello(const char *arguments)
{
    (void)arguments;
    console_write("Hello from a separate Perwix ELF command!\n");
}

bool perwix_module_init(void)
{
    return perwix_command_register("hello", "test a loadable ELF command", command_hello);
}
