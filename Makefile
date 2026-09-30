CC       = gcc
ASM      = nasm
LD       = ld
CFLAGS   = -m32 -ffreestanding -fno-pie -fno-pic -nostdlib \
           -fno-stack-protector -fno-builtin -fno-common \
           -fno-asynchronous-unwind-tables -O2 -Wall -Wextra

all: myos.iso

build/boot.o: src/boot.asm
	@mkdir -p build
	$(ASM) -f elf32 $< -o $@

build/kernel.o: src/kernel.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/myos.elf: build/boot.o build/kernel.o src/linker.ld
	$(LD) -m elf_i386 -T src/linker.ld -o $@ build/boot.o build/kernel.o

myos.iso: build/myos.elf iso/boot/grub/grub.cfg
	@mkdir -p iso/boot/grub
	cp build/myos.elf iso/boot/myos.elf
	grub-mkrescue -o $@ iso

run: myos.iso
	qemu-system-i386 -cdrom myos.iso -m 512

clean:
	rm -rf build myos.iso iso/boot/myos.elf

.PHONY: all run clean