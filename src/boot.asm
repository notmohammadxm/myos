BITS 32

; Multiboot2 header. GRUB scans the beginning of the image for this header.
section .multiboot
align 8
header_start:
    dd 0xE85250D6                  ; Multiboot2 magic
    dd 0                            ; architecture = i386
    dd header_end - header_start    ; header length
    dd -(0xE85250D6 + 0 + (header_end - header_start))

    ; End tag: type = 0, flags = 0, size = 8
    dw 0
    dw 0
    dd 8
header_end:

section .bss
align 16
stack_bottom:
    resb 16384                     ; 16 KiB kernel stack
stack_top:

section .text
global _start
extern kernel_main

_start:
    cli

    ; Install our own flat 32-bit GDT instead of depending on GRUB's GDT.
    lgdt [gdt_descriptor]

    mov ax, 0x10                    ; flat data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp 0x08:.reload_cs             ; reload CS with our code selector

.reload_cs:
    mov esp, stack_top
    and esp, 0xFFFFFFF0             ; 16-byte aligned stack

    ; Multiboot2 contract: EAX = magic, EBX = info structure.
    push ebx
    push eax
    call kernel_main

.hang:
    cli
    hlt
    jmp .hang

section .rodata
align 8

gdt_start:
    dq 0x0000000000000000           ; null descriptor

    ; Code: base 0, limit 4 GiB, ring 0, executable/readable, 32-bit.
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xCF
    db 0x00

    ; Data: base 0, limit 4 GiB, ring 0, writable, 32-bit.
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
