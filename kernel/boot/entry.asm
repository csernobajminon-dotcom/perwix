section .multiboot2
align 8
header_start:
    dd 0xe85250d6
    dd 0
    dd header_end - header_start
    dd -(0xe85250d6 + (header_end - header_start))
    ; Request a 32-bit RGB framebuffer. GRUB/UEFI selects and programs the
    ; actual GPU output mode (which can be routed to HDMI).
    dw 5
    dw 0
    dd 20
    ; Let GRUB choose its preferred/native mode instead of forcing 720p.
    dd 0
    dd 0
    dd 32
    align 8
    dw 0
    dw 0
    dd 8
header_end:

section .text.boot
bits 32
global _start
extern kernel_main

_start:
    cli
    mov [boot_magic], eax
    mov [boot_info], ebx
    mov esp, boot_stack_top

    ; Clear the four-level paging structures.
    mov edi, pml4
    xor eax, eax
    mov ecx, 6144
    rep stosd

    ; Identity map the first 4 GiB with supervisor-only 2 MiB pages.
    mov dword [pml4], pdpt + 3
    mov dword [pml4 + 4], 0
    mov edi, pdpt
    mov eax, page_dirs + 3
    mov ecx, 4
.pdpt_loop:
    mov [edi], eax
    mov dword [edi + 4], 0
    add eax, 4096
    add edi, 8
    loop .pdpt_loop

    mov edi, page_dirs
    xor ebx, ebx
    mov ecx, 2048
.page_loop:
    mov eax, ebx
    shl eax, 21
    or eax, 0x83
    mov [edi], eax
    mov dword [edi + 4], 0
    add edi, 8
    inc ebx
    loop .page_loop

    mov eax, pml4
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xc0000080
    rdmsr
    or eax, 1 << 8
    or eax, 1 << 11
    wrmsr

    mov eax, cr0
    or eax, (1 << 31) | (1 << 16)
    mov cr0, eax

    lgdt [gdt64.pointer]
    jmp 0x08:long_mode_start

bits 64
long_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    mov rsp, boot_stack_top
    xor rbp, rbp

    mov edi, [rel boot_magic]
    mov esi, [rel boot_info]
    call kernel_main

.halt:
    cli
    hlt
    jmp .halt

section .rodata
align 8
gdt64:
    dq 0
    dq 0x00af9a000000ffff
    dq 0x00cf92000000ffff
    dq 0x00affa000000ffff
    dq 0x00cff2000000ffff
.pointer:
    dw $ - gdt64 - 1
    dd gdt64

section .bss
alignb 16
boot_magic: resd 1
boot_info: resd 1
alignb 4096
pml4: resb 4096
pdpt: resb 4096
page_dirs: resb 4 * 4096
alignb 16
boot_stack: resb 16384
boot_stack_top:

section .note.GNU-stack noalloc noexec nowrite progbits
