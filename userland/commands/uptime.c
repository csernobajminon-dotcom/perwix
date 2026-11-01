#include "module.h"
#include "perwix.h"

static void command_uptime(const char *arguments)
{
    (void)arguments;
    console_write("Uptime: ");
    console_write_u64(system_ticks / PERWIX_TIMER_HZ);
    console_write(" seconds\n");
}

bool perwix_module_init(void)
{
    return perwix_command_register("uptime", "show system uptime", command_uptime);
}
