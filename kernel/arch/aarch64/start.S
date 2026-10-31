.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    msr daifset, #0xf
    mrs x0, CurrentEL
    cmp x0, #0x8
    b.eq .Lfrom_el2
    cmp x0, #0x4
    b.ne .Lhalt
    b .Lsetup

.Lfrom_el2:
    mov x1, #0x80000000
    msr hcr_el2, x1
    mov x1, #3
    msr cnthctl_el2, x1
    msr cntvoff_el2, xzr
    mov x1, #0x3c5
    msr spsr_el2, x1
    adr x1, .Lsetup
    msr elr_el2, x1
    eret

.Lsetup:
    ldr x0, =stack_top
    mov sp, x0
    ldr x0, =__bss_start
    ldr x1, =__bss_end
.Lbss_clear:
    cmp x0, x1
    b.hs .Lcall_main
    str xzr, [x0], #8
    b .Lbss_clear

.Lcall_main:
    bl arm64_main
.Lhalt:
    wfe
    b .Lhalt

.section .bss, "aw", %nobits
.align 4
stack_bottom:
    .skip 65536
stack_top:

.section .note.GNU-stack, "", %progbits
