CC       ?= gcc
ASM      ?= nasm
LD       ?= ld

CFLAGS   = -m32 -ffreestanding -fno-pie -fno-pic -nostdlib \
           -fno-stack-protector -fno-builtin -fno-common \
           -fno-asynchronous-unwind-tables -fno-unwind-tables \
           -mno-mmx -mno-sse -mno-sse2 -O2 -Wall -Wextra -Werror
TEST_CC       ?= gcc
TEST_CFLAGS   = -O2 -Wall -Wextra -Werror -Wno-builtin-declaration-mismatch -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast

LDFLAGS  = -m elf_i386 -T src/linker.ld -nostdlib \
           -z max-page-size=0x1000 --build-id=none

KERNEL_OBJS = build/boot.o build/int.o build/kernel.o \
              build/framebuffer.o build/renderer.o build/font.o build/gui.o build/mouse.o

all: myos.iso

test: build/unit-test
	./build/unit-test

build/boot.o: src/boot.asm
	@mkdir -p build
	$(ASM) -f elf32 $< -o $@

build/int.o: src/interrupts.asm
	@mkdir -p build
	$(ASM) -f elf32 $< -o $@

build/kernel.o: src/kernel.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/framebuffer.o: src/graphics/framebuffer.c src/graphics/framebuffer.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/renderer.o: src/graphics/renderer.c src/graphics/renderer.h src/graphics/framebuffer.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/font.o: src/graphics/font.c src/graphics/font.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/gui.o: src/gui/gui.c src/gui/gui.h src/graphics/renderer.h src/graphics/framebuffer.h src/graphics/font.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/mouse.o: src/drivers/mouse.c src/drivers/mouse.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/unit-test: tests/unit.c src/graphics/framebuffer.c src/graphics/renderer.c src/graphics/font.c src/gui/gui.c src/drivers/mouse.c
	@mkdir -p build
	$(TEST_CC) $(TEST_CFLAGS) -o $@ $^

build/myos.elf: $(KERNEL_OBJS) src/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

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

.PHONY: all test verify run clean
