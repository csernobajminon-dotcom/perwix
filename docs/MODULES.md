# Perwix ELF modules

Perwix x86-64 loads modules passed by GRUB through Multiboot2 module tags.
Each file is an ELF64 little-endian `ET_REL` object. At boot the loader:

1. Checks the ELF header and section table.
2. Allocates memory for `SHF_ALLOC` sections and copies `.text`, `.rodata` and
   `.data`; `.bss` is zero-filled by the page allocator.
3. Resolves undefined symbols against the explicit kernel export table in
   `kernel/exec/elf.c`.
4. Applies x86-64 RELA relocations supported by the current loader.
5. Calls the module's `bool perwix_module_init(void)` entry point.

## Commands

A command module includes `module.h` and exports `perwix_module_init`. Its
initializer registers a name, description and function:

```c
#include "module.h"
#include "perwix.h"

static void command_hello(const char *arguments)
{
    (void)arguments;
    console_write("Hello from an ELF module!\n");
}

bool perwix_module_init(void)
{
    return perwix_command_register("hello", "example command", command_hello);
}
```

The build script compiles each command into its own file under
`build/modules/cmd_*.elf`. Add its name to the command loop in `build.sh` and
add a matching `module2` line to `perwix_load_modules` in `grub/grub.cfg`.

## PCI network drivers

A NIC module contains the driver's implementation and a small initializer
that calls `net_register_nic_driver(&nic_driver_...)`. See
`kernel/modules/nic_*.c` for the wrappers. The build links each implementation
and wrapper into an independent `.ko` ELF object. The network stack probes
only drivers that successfully registered before `net_init()` runs.

To add a kernel API that modules may call, add its name and address to the
explicit `kernel_exports` table in `kernel/exec/elf.c`. Do not assume arbitrary
kernel symbols are automatically visible to modules.

## Current security boundary

These are relocatable ELF modules, not isolated user processes. Their init and
command callbacks execute in ring 0, share the kernel address space, and can
access hardware through exported kernel functions. There is no module unload,
module signature verification, dependency resolver, syscall boundary, ring-3
execution or fault isolation yet. A bad module can crash the whole system.
The next step toward Linux-like command execution is a ring-3 process loader
with per-process page tables and a syscall ABI; drivers can later receive a
separate privilege/service boundary.
