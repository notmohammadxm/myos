BITS 32

section .text

extern exception_handler

%macro ISR_NOERR 1
global isr%1
isr%1:
    cld
    push dword %1
    pusha
    call exception_handler
    popa
    add esp, 4
    iretd
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    cld
    push dword %1
    pusha
    call exception_handler
    popa
    add esp, 8
    iretd
%endmacro

; CPU exceptions 0-31.
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

global idt_load
idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret

global irq0_stub
extern irq0_handler
irq0_stub:
    cld
    pusha
    call irq0_handler
    popa
    iretd

global irq1_stub
extern irq1_handler
irq1_stub:
    cld
    pusha
    call irq1_handler
    popa
    iretd

section .note.GNU-stack noalloc noexec nowrite progbits
