bits 64

section .text
global timer_irq
global default_irq
extern timer_handler
extern default_interrupt

timer_irq:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rdi
    push rsi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    and rsp, -16
    call timer_handler
    mov rsp, rax

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rsi
    pop rdi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

default_irq:
    cli
    and rsp, -16
    call default_interrupt
.halt:
    hlt
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite progbits
