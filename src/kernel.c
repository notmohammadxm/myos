#include <stdint.h>
#include <stddef.h>

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEMORY  0xB8000

#define IDT_ENTRIES 256

#define PIC1_CMD    0x20
#define PIC1_DATA   0x21
#define PIC2_CMD    0xA0
#define PIC2_DATA   0xA1

#define KBD_BUF_SIZE 256
#define LINE_MAX     256

static volatile uint16_t* const vga = (volatile uint16_t*)VGA_MEMORY;
static int row = 0;
static int col = 0;
static uint8_t color = 0x0A;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void io_wait(void) {
    outb(0x80, 0);
}

static inline void cpu_halt(void) {
    __asm__ volatile("hlt");
}

static inline void cpu_cli(void) {
    __asm__ volatile("cli");
}

static inline void cpu_sti(void) {
    __asm__ volatile("sti");
}

static void clear_screen(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; ++i) {
        vga[i] = ((uint16_t)color << 8) | ' ';
    }
    row = 0;
    col = 0;
}

static void scroll(void) {
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; ++i) {
        vga[i] = vga[i + VGA_WIDTH];
    }
    for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; ++i) {
        vga[i] = ((uint16_t)color << 8) | ' ';
    }
    row = VGA_HEIGHT - 1;
}

static void putc(char c) {
    if (c == '\n') {
        col = 0;
        ++row;
    } else if (c == '\r') {
        col = 0;
        return;
    } else if (c == '\b') {
        if (col > 0) {
            --col;
            vga[row * VGA_WIDTH + col] = ((uint16_t)color << 8) | ' ';
        }
        return;
    } else if (c == '\t') {
        int spaces = 4 - (col % 4);
        while (spaces-- > 0) {
            putc(' ');
        }
        return;
    } else {
        vga[row * VGA_WIDTH + col] = ((uint16_t)color << 8) | (uint8_t)c;
        ++col;
    }

    if (col >= VGA_WIDTH) {
        col = 0;
        ++row;
    }
    if (row >= VGA_HEIGHT) {
        scroll();
    }
}

static void print(const char* s) {
    while (*s) {
        putc(*s++);
    }
}

static void print_color(const char* s, uint8_t c) {
    uint8_t old = color;
    color = c;
    print(s);
    color = old;
}

static void print_uint(uint32_t value) {
    char buffer[11];
    int i = 0;

    if (value == 0) {
        putc('0');
        return;
    }

    while (value > 0 && i < (int)sizeof(buffer)) {
        buffer[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        putc(buffer[--i]);
    }
}

struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtp;

extern void idt_load(uint32_t idt_address);
extern void irq0_stub(void);
extern void irq1_stub(void);

#define DECL_ISR(n) extern void isr##n(void)
DECL_ISR(0);  DECL_ISR(1);  DECL_ISR(2);  DECL_ISR(3);
DECL_ISR(4);  DECL_ISR(5);  DECL_ISR(6);  DECL_ISR(7);
DECL_ISR(8);  DECL_ISR(9);  DECL_ISR(10); DECL_ISR(11);
DECL_ISR(12); DECL_ISR(13); DECL_ISR(14); DECL_ISR(15);
DECL_ISR(16); DECL_ISR(17); DECL_ISR(18); DECL_ISR(19);
DECL_ISR(20); DECL_ISR(21); DECL_ISR(22); DECL_ISR(23);
DECL_ISR(24); DECL_ISR(25); DECL_ISR(26); DECL_ISR(27);
DECL_ISR(28); DECL_ISR(29); DECL_ISR(30); DECL_ISR(31);
#undef DECL_ISR

static void idt_set_gate(int n, uint32_t handler) {
    if (n < 0 || n >= IDT_ENTRIES) {
        return;
    }

    idt[n].base_lo = (uint16_t)(handler & 0xFFFFu);
    idt[n].sel = 0x08;             /* MyOS kernel code selector. */
    idt[n].always0 = 0;
    idt[n].flags = 0x8E;           /* Present, ring 0, 32-bit interrupt gate. */
    idt[n].base_hi = (uint16_t)((handler >> 16) & 0xFFFFu);
}

static void idt_init(void) {
    for (int i = 0; i < IDT_ENTRIES; ++i) {
        idt[i].base_lo = 0;
        idt[i].sel = 0;
        idt[i].always0 = 0;
        idt[i].flags = 0;
        idt[i].base_hi = 0;
    }

#define SET_ISR(n) idt_set_gate((n), (uint32_t)isr##n)
    SET_ISR(0);  SET_ISR(1);  SET_ISR(2);  SET_ISR(3);
    SET_ISR(4);  SET_ISR(5);  SET_ISR(6);  SET_ISR(7);
    SET_ISR(8);  SET_ISR(9);  SET_ISR(10); SET_ISR(11);
    SET_ISR(12); SET_ISR(13); SET_ISR(14); SET_ISR(15);
    SET_ISR(16); SET_ISR(17); SET_ISR(18); SET_ISR(19);
    SET_ISR(20); SET_ISR(21); SET_ISR(22); SET_ISR(23);
    SET_ISR(24); SET_ISR(25); SET_ISR(26); SET_ISR(27);
    SET_ISR(28); SET_ISR(29); SET_ISR(30); SET_ISR(31);
#undef SET_ISR

    idt_set_gate(32, (uint32_t)irq0_stub);
    idt_set_gate(33, (uint32_t)irq1_stub);

    idtp.limit = (uint16_t)(sizeof(idt) - 1);
    idtp.base = (uint32_t)&idt[0];
    idt_load((uint32_t)&idtp);
}

static void pic_remap(void) {
    /* Disable IRQs while reprogramming the PIC. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();

    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();

    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();

    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    /* Only timer (IRQ0) and keyboard (IRQ1) are enabled. */
    outb(PIC1_DATA, 0xFC);
    outb(PIC2_DATA, 0xFF);
}

static void pic_eoi(int irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20);
    }
    outb(PIC1_CMD, 0x20);
}

static void pit_init(uint32_t frequency_hz) {
    if (frequency_hz == 0) {
        return;
    }

    uint32_t divisor = 1193180u / frequency_hz;
    if (divisor == 0 || divisor > 0xFFFFu) {
        return;
    }

    outb(0x43, 0x36);              /* Channel 0, lobyte/hibyte, mode 3. */
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

static const char kbd_map[128] = {
    [0x02]='1', [0x03]='2', [0x04]='3', [0x05]='4', [0x06]='5',
    [0x07]='6', [0x08]='7', [0x09]='8', [0x0A]='9', [0x0B]='0',
    [0x0C]='-', [0x0D]='=', [0x0E]='\b', [0x0F]='\t',
    [0x10]='q', [0x11]='w', [0x12]='e', [0x13]='r', [0x14]='t',
    [0x15]='y', [0x16]='u', [0x17]='i', [0x18]='o', [0x19]='p',
    [0x1A]='[', [0x1B]=']', [0x1C]='\n',
    [0x1E]='a', [0x1F]='s', [0x20]='d', [0x21]='f', [0x22]='g',
    [0x23]='h', [0x24]='j', [0x25]='k', [0x26]='l', [0x27]=';',
    [0x28]='\'', [0x29]='`', [0x2B]='\\',
    [0x2C]='z', [0x2D]='x', [0x2E]='c', [0x2F]='v', [0x30]='b',
    [0x31]='n', [0x32]='m', [0x33]=',', [0x34]='.', [0x35]='/',
    [0x39]=' '
};

static const char kbd_shift_map[128] = {
    [0x02]='!', [0x03]='@', [0x04]='#', [0x05]='$', [0x06]='%',
    [0x07]='^', [0x08]='&', [0x09]='*', [0x0A]='(', [0x0B]=')',
    [0x0C]='_', [0x0D]='+', [0x1A]='{', [0x1B]='}', [0x27]=':',
    [0x28]='"', [0x29]='~', [0x2B]='|', [0x33]='<', [0x34]='>',
    [0x35]='?'
};

static volatile char kbd_buf[KBD_BUF_SIZE];
static volatile uint16_t kbd_head = 0;
static volatile uint16_t kbd_tail = 0;
static volatile uint32_t timer_ticks = 0;
static volatile uint8_t shift_down = 0;
static volatile uint8_t caps_lock = 0;
static volatile uint8_t extended_scancode = 0;

static int kbd_get(void) {
    if (kbd_head == kbd_tail) {
        return -1;
    }

    char c = kbd_buf[kbd_tail];
    kbd_tail = (uint16_t)((kbd_tail + 1) % KBD_BUF_SIZE);
    return (unsigned char)c;
}

static void kbd_push(char c) {
    uint16_t next = (uint16_t)((kbd_head + 1) % KBD_BUF_SIZE);
    if (next == kbd_tail) {
        return;                         /* Buffer full: drop this character. */
    }

    kbd_buf[kbd_head] = c;
    kbd_head = next;
}

static char kbd_translate(uint8_t sc) {
    char c = kbd_map[sc];
    if (!c) {
        return 0;
    }

    if (c >= 'a' && c <= 'z') {
        if (caps_lock ^ shift_down) {
            return (char)(c - 'a' + 'A');
        }
        return c;
    }

    if (shift_down && kbd_shift_map[sc]) {
        return kbd_shift_map[sc];
    }

    return c;
}

void irq0_handler(void) {
    ++timer_ticks;
    pic_eoi(0);
}

void irq1_handler(void) {
    uint8_t sc = inb(0x60);

    if (sc == 0xE0) {
        extended_scancode = 1;
        pic_eoi(1);
        return;
    }

    if (extended_scancode) {
        extended_scancode = 0;
        pic_eoi(1);
        return;                         /* Arrow keys, navigation, etc. for now. */
    }

    uint8_t released = (uint8_t)(sc & 0x80u);
    uint8_t code = (uint8_t)(sc & 0x7Fu);

    if (code == 0x2A || code == 0x36) {  /* Left/right Shift. */
        shift_down = (uint8_t)!released;
        pic_eoi(1);
        return;
    }

    if (released) {
        pic_eoi(1);
        return;
    }

    if (code == 0x3A) {                  /* Caps Lock. */
        caps_lock = (uint8_t)!caps_lock;
        pic_eoi(1);
        return;
    }

    char c = kbd_translate(code);
    if (c) {
        kbd_push(c);
    }

    pic_eoi(1);
}

static int kbc_ready(void) {
    for (volatile uint32_t i = 0; i < 100000u; ++i) {
        if ((inb(0x64) & 0x02u) == 0) {
            return 1;
        }
    }
    return 0;
}

static void shutdown(void) {
    print_color("\n[*] Shutting down MyOS...\n", 0x0C);

    /*
     * VMware/PIIX4 ACPI soft-off.
     * Port 0x604 is commonly used by QEMU, while VMware
     * exposes ACPI/PIIX4 power management rather than relying
     * on the QEMU-specific shutdown port.
     */

    /* Disable interrupts before power-off request. */
    __asm__ volatile("cli");

    /* Try PIIX4/ACPI soft-off. */
    outw(0xB004, 0x2000);

    /* If the virtual hardware does not accept it, stop safely. */
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void reboot(void) {
    print_color("\n[*] Rebooting MyOS...\n", 0x0E);
    cpu_cli();

    /* Try the traditional keyboard-controller reset first. */
    if (kbc_ready()) {
        outb(0x64, 0xFE);
        for (volatile uint32_t i = 0; i < 100000u; ++i) {
            /* Wait briefly for the hardware/VM to reset. */
        }
    }

    /* Common chipset reset register fallback. */
    outb(0xCF9, 0x06);

    for (;;) {
        cpu_halt();
    }
}

static void draw_banner(void) {
    print_color("  __  __       ___  ____  \n", 0x0B);
    print_color(" |  \\/  |_   _/ _ \\/ ___| \n", 0x0B);
    print_color(" | |\\/| | | | | | | \\___ \\ \n", 0x0B);
    print_color(" | |  | | |_| | |_| |___) |\n", 0x0B);
    print_color(" |_|  |_|\\__, |\\___/|____/ \n", 0x0B);
    print_color("         |___/             \n", 0x0B);
    print_color("        Terminal v0.4\n", 0x08);
    print_color("----------------------------------------\n\n", 0x08);
}

static char line[LINE_MAX];
static int line_len = 0;

static int streq(const char* a, const char* b) {
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static void prompt(void) {
    print_color("myos", 0x0A);
    print_color("> ", 0x0F);
}

static void cmd_help(void) {
    print("Available commands:\n");
    print("  help        - show this message\n");
    print("  version     - show kernel version\n");
    print("  uptime      - show timer ticks / uptime\n");
    print("  ascii       - redraw the banner\n");
    print("  clear       - clear the screen\n");
    print("  echo TEXT   - print TEXT\n");
    print("  reboot      - restart the machine\n");
    print("  shutdown    - power off the machine\n");
}

static void cmd_version(void) {
    print("MyOS v0.4 \"Terminal\"\n");
    print("Kernel: 32-bit i386, Multiboot2\n");
    print("Build: GitHub Actions\n");
}

static void cmd_uptime(void) {
    uint32_t ticks = timer_ticks;
    print("Timer ticks: ");
    print_uint(ticks);
    print("\nApprox. seconds: ");
    print_uint(ticks / 100u);
    print("\n");
}

static void execute(const char* cmd) {
    while (*cmd == ' ') {
        ++cmd;
    }

    if (*cmd == 0) {
        return;
    }

    if (streq(cmd, "help")) {
        cmd_help();
    } else if (streq(cmd, "version")) {
        cmd_version();
    } else if (streq(cmd, "uptime")) {
        cmd_uptime();
    } else if (streq(cmd, "ascii")) {
        draw_banner();
    } else if (streq(cmd, "clear")) {
        clear_screen();
        draw_banner();
    } else if (streq(cmd, "reboot")) {
        reboot();
    } else if (streq(cmd, "shutdown")) {
        shutdown();
    } else if (streq(cmd, "echo")) {
        putc('\n');
    } else if (cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'h' &&
               cmd[3] == 'o' && cmd[4] == ' ') {
        print(cmd + 5);
        putc('\n');
    } else {
        print_color("Unknown command: ", 0x0C);
        print(cmd);
        putc('\n');
        print("Type 'help' for commands.\n");
    }
}

static const char* const exception_names[32] = {
    "Division by zero",                "Debug",                    "Non-maskable interrupt",
    "Breakpoint",                      "Overflow",                 "Bound range exceeded",
    "Invalid opcode",                  "Device not available",     "Double fault",
    "Coprocessor segment overrun",     "Invalid TSS",               "Segment not present",
    "Stack-segment fault",             "General protection fault", "Page fault",
    "Reserved",                        "x87 floating-point",        "Alignment check",
    "Machine check",                   "SIMD floating-point",       "Virtualization",
    "Control protection",              "Reserved",                  "Reserved",
    "Reserved",                        "Reserved",                  "Reserved",
    "Reserved",                        "Hypervisor injection",      "VMM communication",
    "Security exception",              "Reserved"
};


void exception_handler(uint32_t interrupt_number) {
    cpu_cli();
    print_color("\n\n!!! KERNEL EXCEPTION !!!\n", 0x0C);

    if (interrupt_number < 32) {
        print_color(exception_names[interrupt_number], 0x0E);
    } else {
        print_color("Unknown exception", 0x0E);
    }

    print(" (#");
    print_uint(interrupt_number);
    print(")\nSystem halted.\n");

    for (;;) {
        cpu_halt();
    }
}

void kernel_main(uint32_t magic, void* mb_info) {
    (void)mb_info;
    clear_screen();

    if (magic != 0x36D76289u) {
        print_color("Bad Multiboot2 magic: ", 0x0C);
        print_uint(magic);
        print("\nSystem halted.\n");
        cpu_cli();
        for (;;) {
            cpu_halt();
        }
    }

    idt_init();
    pic_remap();
    pit_init(100u);

    draw_banner();
    print("Welcome to ");
    print_color("MyOS", 0x0A);
    print(". Type 'help' to get started.\n\n");

    cpu_sti();

    prompt();
    for (;;) {
        int c;
        while ((c = kbd_get()) >= 0) {
            if (c == '\n') {
                putc('\n');
                line[line_len] = 0;
                execute(line);
                line_len = 0;
                prompt();
            } else if (c == '\b') {
                if (line_len > 0) {
                    --line_len;
                    putc('\b');
                }
            } else if (c >= 32 && c < 127 && line_len < LINE_MAX - 1) {
                line[line_len++] = (char)c;
                putc((char)c);
            }
        }

        /* Save CPU while waiting for the next hardware interrupt. */
        __asm__ volatile("hlt");
    }
}
