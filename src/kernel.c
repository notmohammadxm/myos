#include <stdint.h>
#include <stddef.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

static volatile uint16_t* vga = (volatile uint16_t*)VGA_MEMORY;
static int row = 0;
static int col = 0;
static uint8_t color = 0x0A;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" :: "a"(val), "Nd"(port));
}
static inline void io_wait(void) { outb(0x80, 0); }

static void clear_screen(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga[i] = ((uint16_t)color << 8) | ' ';
    row = 0;
    col = 0;
}

static void scroll(void) {
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
        vga[i] = vga[i + VGA_WIDTH];
    for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++)
        vga[i] = ((uint16_t)color << 8) | ' ';
    row = VGA_HEIGHT - 1;
}

static void putc(char c) {
    if (c == '\n') {
        col = 0;
        row++;
    } else if (c == '\r') {
        col = 0;
        return;
    } else if (c == '\b') {
        if (col > 0) {
            col--;
            vga[row * VGA_WIDTH + col] = ((uint16_t)color << 8) | ' ';
        }
        return;
    } else {
        vga[row * VGA_WIDTH + col] = ((uint16_t)color << 8) | (uint8_t)c;
        col++;
    }
    if (col >= VGA_WIDTH) { col = 0; row++; }
    if (row >= VGA_HEIGHT) scroll();
}

static void print(const char* s) { while (*s) putc(*s++); }

static void print_color(const char* s, uint8_t c) {
    uint8_t old = color;
    color = c;
    print(s);
    color = old;
}

struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t always0;
    uint8_t flags;
    uint16_t base_hi;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtp;

extern void idt_load(uint32_t);
extern void irq0_stub(void);
extern void irq1_stub(void);

static void idt_set_gate(int n, uint32_t handler) {
    idt[n].base_lo = handler & 0xFFFF;
    idt[n].sel     = 0x08;
    idt[n].always0 = 0;
    idt[n].flags   = 0x8E;
    idt[n].base_hi = (handler >> 16) & 0xFFFF;
}

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

static void pic_remap(void) {
    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, 0xFC);
    outb(PIC2_DATA, 0xFF);
}

static void pic_eoi(int irq) {
    if (irq >= 8) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

static const char kbd_map[128] = {
    0, 27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
};

#define KBD_BUF_SIZE 256
static volatile char kbd_buf[KBD_BUF_SIZE];
static volatile int kbd_head = 0;
static volatile int kbd_tail = 0;

static int kbd_get(void) {
    if (kbd_head == kbd_tail) return -1;
    char c = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return c;
}

void irq0_handler(void) {
    pic_eoi(0);
}

void irq1_handler(void) {
    uint8_t sc = inb(0x60);
    if (!(sc & 0x80)) {
        char c = kbd_map[sc & 0x7F];
        if (c) {
            kbd_buf[kbd_head] = c;
            kbd_head = (kbd_head + 1) % KBD_BUF_SIZE;
        }
    }
    pic_eoi(1);
}

static void shutdown(void) {
    print_color("\n[*] Shutting down MyOS...\n", 0x0C);
    outw(0x604,  0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile("cli; hlt");
}

static void reboot(void) {
    print_color("\n[*] Rebooting...\n", 0x0E);
    for (volatile int i = 0; i < 5000000; i++);
    outb(0x64, 0xFE);
    for (;;) __asm__ volatile("cli; hlt");
}

static void draw_banner(void) {
    print_color("  __  __       ___  ____  \n", 0x0B);
    print_color(" |  \\/  |_   _/ _ \\/ ___| \n", 0x0B);
    print_color(" | |\\/| | | | | | | \\___ \\ \n", 0x0B);
    print_color(" | |  | | |_| | |_| |___) |\n", 0x0B);
    print_color(" |_|  |_|\\__, |\\___/|____/ \n", 0x0B);
    print_color("         |___/             \n", 0x0B);
    print_color("        Terminal v0.3\n", 0x08);
    print_color("----------------------------------------\n\n", 0x08);
}

#define LINE_MAX 256
static char line[LINE_MAX];
static int line_len = 0;

static int streq(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
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
    print("  ascii       - redraw the banner\n");
    print("  clear       - clear the screen\n");
    print("  echo TEXT   - print TEXT\n");
    print("  reboot      - restart the machine\n");
    print("  shutdown    - power off the machine\n");
}

static void cmd_version(void) {
    print("MyOS v0.3 \"Terminal\"\n");
    print("Kernel: 32-bit i386, Multiboot2\n");
    print("Built 100%% on GitHub Actions\n");
}

static void execute(const char* cmd) {
    while (*cmd == ' ') cmd++;
    if (*cmd == 0) return;

    if (streq(cmd, "help"))            cmd_help();
    else if (streq(cmd, "version"))    cmd_version();
    else if (streq(cmd, "ascii"))      draw_banner();
    else if (streq(cmd, "clear"))    { clear_screen(); draw_banner(); }
    else if (streq(cmd, "reboot"))     reboot();
    else if (streq(cmd, "shutdown"))   shutdown();
    else if (cmd[0]=='e' && cmd[1]=='c' && cmd[2]=='h' &&
             cmd[3]=='o' && cmd[4]==' ') {
        print(cmd + 5);
        putc('\n');
    } else {
        print_color("Unknown command: ", 0x0C);
        print(cmd);
        putc('\n');
        print("Type 'help' for commands.\n");
    }
}

void kernel_main(uint32_t magic, void* mb_info) {
    (void)mb_info;
    clear_screen();

    for (int i = 0; i < 256; i++) idt_set_gate(i, 0);
    idt_set_gate(32, (uint32_t)irq0_stub);
    idt_set_gate(33, (uint32_t)irq1_stub);
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;
    idt_load((uint32_t)&idtp);

    pic_remap();

    if (magic != 0x36D76289) {
        print_color("Bad multiboot magic. Halting.\n", 0x0C);
        for (;;) __asm__ volatile("cli; hlt");
    }

    draw_banner();
    print("Welcome to ");
    print_color("MyOS", 0x0A);
    print(". Type 'help' to get started.\n\n");

    __asm__ volatile("sti");

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
                if (line_len > 0) { line_len--; putc('\b'); }
            } else if (c >= 32 && c < 127 && line_len < LINE_MAX - 1) {
                line[line_len++] = c;
                putc(c);
            }
        }
        __asm__ volatile("hlt");
    }
}