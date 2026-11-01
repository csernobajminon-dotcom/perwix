#ifndef PERWIX_MODULE_H
#define PERWIX_MODULE_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*perwix_command_fn)(const char *arguments);

/* Commands are registered by independently linked ELF command modules. */
bool perwix_command_register(const char *name, const char *description,
                             perwix_command_fn execute);
bool perwix_command_execute(const char *name, const char *arguments);
void perwix_command_list(void);
uint32_t perwix_module_count(void);
const char *perwix_module_name(uint32_t index);

/* Load Multiboot2 module tags. Modules must be x86-64 ELF ET_REL files and
 * export bool perwix_module_init(void). They currently execute in ring 0. */
uint32_t perwix_modules_load(uint64_t multiboot_info_address);

#endif
