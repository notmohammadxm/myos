BITS 32

; Multiboot2 header.
section .multiboot
align 8
header_start:
    dd 0xE85250D6
    dd 0
    dd header_end - header_start
    dd -(0xE85250D6 + 0 + (header_end - header_start))

    ; Request a 1024x768x32 framebuffer when available.
    dw 5
    dw 0
    dd 20
    dd 1024
    dd 768
    dd 32

    ; Padding: each Multiboot2 header tag starts on an 8-byte boundary.
    dd 0

    ; Multiboot2 header end tag.
    dw 0
    dw 0
    dd 8
header_end:

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .text
global _start
extern kernel_main

_start:
    cli

    ; Preserve the Multiboot2 values before touching AX.
    ; EAX = magic, EBX = multiboot info pointer.
    mov ecx, eax
    mov edx, ebx

    ; Install our own flat protected-mode GDT.
    lgdt [gdt_descriptor]

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp 0x08:.reload_cs

.reload_cs:
    mov esp, stack_top
    and esp, 0xFFFFFFF0

    ; C calling convention: magic, mb_info.
    push edx
    push ecx
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .rodata
align 8

gdt_start:
    dq 0x0000000000000000

    ; Kernel code selector 0x08.
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xCF
    db 0x00

    ; Kernel data selector 0x10.
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92
    db 0xCF
    db 0x00

gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start
