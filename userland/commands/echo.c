#include "module.h"
#include "perwix.h"

static void command_echo(const char *arguments)
{
    console_write(arguments != 0 ? arguments : "");
    console_write("\n");
}

bool perwix_module_init(void)
{
    return perwix_command_register("echo", "print text", command_echo);
}
