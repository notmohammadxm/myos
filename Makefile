CC       ?= gcc
ASM      ?= nasm
LD       ?= ld

CFLAGS   = -m32 -ffreestanding -fno-pie -fno-pic -nostdlib \
           -fno-stack-protector -fno-builtin -fno-common \
           -fno-asynchronous-unwind-tables -fno-unwind-tables \
           -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra

LDFLAGS  = -m elf_i386 -T src/linker.ld -nostdlib \
           -z max-page-size=0x1000 --build-id=none

all: myos.iso

build/boot.o: src/boot.asm
	@mkdir -p build
	$(ASM) -f elf32 $< -o $@

build/int.o: src/interrupts.asm
	@mkdir -p build
	$(ASM) -f elf32 $< -o $@

build/kernel.o: src/kernel.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/myos.elf: build/boot.o build/int.o build/kernel.o src/linker.ld
	$(LD) $(LDFLAGS) -o $@ \
		build/boot.o build/int.o build/kernel.o

myos.iso: build/myos.elf iso/boot/grub/grub.cfg
	@mkdir -p iso/boot/grub
	cp build/myos.elf iso/boot/myos.elf
	grub-mkrescue -o $@ iso

verify: build/myos.elf
	grub-file --is-x86-multiboot2 $<

run: myos.iso
	qemu-system-i386 -cdrom myos.iso -m 512M

clean:
	rm -rf build myos.iso iso/boot/myos.elf

.PHONY: all verify run clean
