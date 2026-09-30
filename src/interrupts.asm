section .text

global idt_load
idt_load:
    mov eax, [esp+4]
    lidt [eax]
    ret

global irq0_stub
extern irq0_handler
irq0_stub:
    pusha
    call irq0_handler
    popa
    iretd

global irq1_stub
extern irq1_handler
irq1_stub:
    pusha
    call irq1_handler
    popa
    iretd

section .note.GNU-stack noalloc noexec nowrite progbits
