#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * MyOS v0.5 - text-only desktop-style operating environment
 * Features:
 *   calculator, themes, history, tab completion, profile,
 *   system info, RTC clock/calendar, task manager,
 *   help center, settings, notifications, logs, network tools.
 * ============================================================ */

#define VGA_WIDTH       80
#define VGA_HEIGHT      25
#define VGA_MEMORY      0xB8000
#define IDT_ENTRIES     256
#define KBD_BUF_SIZE    256
#define LINE_MAX        72
#define HISTORY_COUNT   16
#define LOG_COUNT       32
#define LOG_LEN         96
#define NOTIFY_COUNT    8
#define NOTIFY_LEN      80

#define PIC1_CMD        0x20
#define PIC1_DATA       0x21
#define PIC2_CMD        0xA0
#define PIC2_DATA       0xA1

#define KEY_UP          0x80
#define KEY_DOWN        0x81
#define KEY_LEFT        0x82
#define KEY_RIGHT       0x83
#define KEY_DELETE      0x84

static volatile uint16_t* const vga = (volatile uint16_t*)VGA_MEMORY;
static int row = 0;
static int col = 0;
static uint8_t color = 0x0A;

static volatile uint8_t kbd_buf[KBD_BUF_SIZE];
static volatile uint16_t kbd_head = 0;
static volatile uint16_t kbd_tail = 0;

static volatile uint32_t timer_ticks = 0;
static volatile uint8_t shift_down = 0;
static volatile uint8_t caps_lock = 0;
static volatile uint8_t extended_scancode = 0;

static char line[LINE_MAX];
static int line_len = 0;
static int cursor_pos = 0;

static char history[HISTORY_COUNT][LINE_MAX];
static int history_count = 0;
static int history_cursor = 0;

static char username[16] = "admin";
static char hostname[24] = "myos";
static const char* current_theme = "matrix";

static char logs[LOG_COUNT][LOG_LEN];
static int log_count = 0;
static int log_head = 0;

static char notifications[NOTIFY_COUNT][NOTIFY_LEN];
static int notify_count = 0;
static int notify_head = 0;

static const char* bootloader_name = "Unknown";
static uint32_t mem_lower_kib = 0;
static uint32_t mem_upper_kib = 0;

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

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
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

static size_t str_len(const char* s) {
    size_t n = 0;
    while (s && s[n]) ++n;
    return n;
}

static int streq(const char* a, const char* b) {
    while (*a && *b && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static int starts_with(const char* s, const char* prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static void str_copy(char* dst, const char* src, int max_len) {
    int i = 0;
    if (max_len <= 0) return;
    while (src[i] && i < max_len - 1) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
}

static void print(const char* s);
static void draw_banner(void);
static void prompt(void);
static void pic_eoi(int irq);

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
        while (spaces-- > 0) putc(' ');
        return;
    } else {
        vga[row * VGA_WIDTH + col] =
            ((uint16_t)color << 8) | (uint8_t)c;
        ++col;
    }

    if (col >= VGA_WIDTH) {
        col = 0;
        ++row;
    }
    if (row >= VGA_HEIGHT) scroll();
}

static void print(const char* s) {
    while (*s) putc(*s++);
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
        buffer[i++] = (char)('0' + (value % 10u));
        value /= 10u;
    }

    while (i > 0) putc(buffer[--i]);
}

static void print_int(int32_t value) {
    if (value < 0) {
        putc('-');
        value = -value;
    }
    print_uint((uint32_t)value);
}

static void print_hex8(uint8_t value) {
    static const char* hex = "0123456789ABCDEF";
    putc(hex[(value >> 4) & 0x0F]);
    putc(hex[value & 0x0F]);
}

static void print_hex16(uint16_t value) {
    print_hex8((uint8_t)(value >> 8));
    print_hex8((uint8_t)value);
}


static void trim_spaces(char* s) {
    int len = (int)str_len(s);
    int start = 0;
    while (s[start] == ' ') ++start;

    int end = len - 1;
    while (end >= start && s[end] == ' ') --end;

    int j = 0;
    for (int i = start; i <= end; ++i) s[j++] = s[i];
    s[j] = 0;
}

/* ---------- logging / notifications ---------- */

static void log_event(const char* message) {
    str_copy(logs[log_head], message, LOG_LEN);
    log_head = (log_head + 1) % LOG_COUNT;
    if (log_count < LOG_COUNT) ++log_count;
}

static void notify_add(const char* message) {
    str_copy(notifications[notify_head], message, NOTIFY_LEN);
    notify_head = (notify_head + 1) % NOTIFY_COUNT;
    if (notify_count < NOTIFY_COUNT) ++notify_count;
}

static void cmd_logs(const char* arg) {
    if (streq(arg, "clear")) {
        log_count = 0;
        log_head = 0;
        print("Logs cleared.\n");
        return;
    }

    if (log_count == 0) {
        print("No log entries.\n");
        return;
    }

    print("Kernel/Shell logs:\n");
    int start = (log_count < LOG_COUNT) ? 0 : log_head;
    for (int i = 0; i < log_count; ++i) {
        int idx = (start + i) % LOG_COUNT;
        print("  [");
        print_uint((uint32_t)(i + 1));
        print("] ");
        print(logs[idx]);
        putc('\n');
    }
}

static void cmd_notifications(const char* arg) {
    if (streq(arg, "clear")) {
        notify_count = 0;
        notify_head = 0;
        print("Notifications cleared.\n");
        return;
    }

    if (notify_count == 0) {
        print("No notifications.\n");
        return;
    }

    print("Notifications:\n");
    int start = (notify_count < NOTIFY_COUNT) ? 0 : notify_head;
    for (int i = 0; i < notify_count; ++i) {
        int idx = (start + i) % NOTIFY_COUNT;
        print("  - ");
        print(notifications[idx]);
        putc('\n');
    }
}

static void cmd_notify(const char* text) {
    if (!*text) {
        print("Usage: notify MESSAGE\n");
        return;
    }
    notify_add(text);
    log_event("Notification created");
    print("Notification added.\n");
}

/* ---------- themes ---------- */

struct theme_def {
    const char* name;
    uint8_t fg;
    uint8_t bg;
};

static const struct theme_def themes[] = {
    { "matrix", 0x0A, 0x00 },
    { "ice",    0x0B, 0x01 },
    { "amber",  0x0E, 0x00 },
    { "mono",   0x0F, 0x00 }
};

static void apply_theme(const struct theme_def* t) {
    color = (uint8_t)((t->bg << 4) | (t->fg & 0x0F));
    current_theme = t->name;
    clear_screen();
    print_color("Theme changed to ", t->fg);
    print(t->name);
    print(".\n\n");
    log_event("Theme changed");
}

static void cmd_theme(const char* arg) {
    if (!*arg) {
        print("Current theme: ");
        print(current_theme);
        print("\nAvailable themes: matrix ice amber mono\n");
        return;
    }

    for (size_t i = 0; i < sizeof(themes) / sizeof(themes[0]); ++i) {
        if (streq(arg, themes[i].name)) {
            apply_theme(&themes[i]);
            draw_banner();
            prompt();
            return;
        }
    }

    print("Unknown theme. Use: matrix, ice, amber, mono\n");
}

/* ---------- RTC ---------- */

struct rtc_time {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t weekday;
    uint16_t year;
};

static uint8_t rtc_read_reg(uint8_t reg) {
    outb(0x70, (uint8_t)(reg | 0x80));
    return inb(0x71);
}

static uint8_t bcd_to_bin(uint8_t x) {
    return (uint8_t)((x & 0x0F) + ((x >> 4) * 10));
}

static int days_in_month(int year, int month) {
    static const uint8_t days[] =
        {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2) {
        int leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
        return leap ? 29 : 28;
    }
    return days[month - 1];
}

static uint8_t weekday_calc(int year, int month, int day) {
    static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (month < 3) --year;
    return (uint8_t)((year + year/4 - year/100 + year/400 +
                      t[month-1] + day) % 7);
}

static void rtc_read(struct rtc_time* t) {
    while (rtc_read_reg(0x0A) & 0x80) { }

    uint8_t sec = rtc_read_reg(0x00);
    uint8_t min = rtc_read_reg(0x02);
    uint8_t hour = rtc_read_reg(0x04);
    uint8_t day = rtc_read_reg(0x07);
    uint8_t month = rtc_read_reg(0x08);
    uint8_t year = rtc_read_reg(0x09);
    uint8_t century = rtc_read_reg(0x32);
    uint8_t reg_b = rtc_read_reg(0x0B);

    uint8_t binary = (reg_b & 0x04) != 0;

    if (!binary) {
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hour = (uint8_t)((hour & 0x80) | bcd_to_bin(hour & 0x7F));
        day = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year = bcd_to_bin(year);
        century = bcd_to_bin(century);
    }

    if (!(reg_b & 0x02)) {
        uint8_t pm = hour & 0x80;
        hour &= 0x7F;
        if (pm) {
            if (hour < 12) hour = (uint8_t)(hour + 12);
        } else if (hour == 12) {
            hour = 0;
        }
    }

    uint16_t full_year = century ? (uint16_t)(century * 100u + year)
                                 : (uint16_t)(2000u + year);

    t->sec = sec;
    t->min = min;
    t->hour = hour;
    t->day = day;
    t->month = month;
    t->year = full_year;
    t->weekday = weekday_calc(full_year, month, day);
}

static void print_2d(uint32_t n) {
    if (n < 10) putc('0');
    print_uint(n);
}

static const char* month_name(int m) {
    static const char* names[] = {
        "January","February","March","April","May","June",
        "July","August","September","October","November","December"
    };
    if (m < 1 || m > 12) return "Unknown";
    return names[m - 1];
}

static const char* weekday_name(int d) {
    static const char* names[] = {
        "Sunday","Monday","Tuesday","Wednesday",
        "Thursday","Friday","Saturday"
    };
    if (d < 0 || d > 6) return "Unknown";
    return names[d];
}

static void print_time_value(const struct rtc_time* t) {
    print_2d(t->hour);
    putc(':');
    print_2d(t->min);
    putc(':');
    print_2d(t->sec);
}

static void cmd_time(void) {
    struct rtc_time t;
    rtc_read(&t);
    print_time_value(&t);
    print(" - ");
    print(weekday_name(t.weekday));
    print(", ");
    print(month_name(t.month));
    putc(' ');
    print_uint(t.day);
    putc(' ');
    print_uint(t.year);
    putc('\n');
}

static int kbd_has_data(void) {
    return kbd_head != kbd_tail;
}

static int kbd_get_event(void) {
    if (!kbd_has_data()) return -1;
    uint8_t c = kbd_buf[kbd_tail];
    kbd_tail = (uint16_t)((kbd_tail + 1) % KBD_BUF_SIZE);
    return c;
}

static void cmd_clock(void) {
    print("Live clock. Press any key to exit.\n");
    uint32_t last = timer_ticks;
    for (;;) {
        if (kbd_has_data()) {
            (void)kbd_get_event();
            putc('\n');
            return;
        }

        if ((uint32_t)(timer_ticks - last) >= 100u) {
            last = timer_ticks;
            struct rtc_time t;
            rtc_read(&t);
            print("\rTime ");
            print_time_value(&t);
            print("        ");
        }
        cpu_halt();
    }
}

static void cmd_calendar(void) {
    struct rtc_time t;
    rtc_read(&t);
    int y = t.year;
    int m = t.month;
    int first = weekday_calc(y, m, 1);
    int days = days_in_month(y, m);

    putc('\n');
    print("        ");
    print(month_name(m));
    putc(' ');
    print_uint((uint32_t)y);
    putc('\n');
    print("Su Mo Tu We Th Fr Sa\n");

    for (int i = 0; i < first; ++i) print("   ");
    for (int d = 1; d <= days; ++d) {
        if (d < 10) putc(' ');
        print_uint((uint32_t)d);
        if ((first + d) % 7 == 0) putc('\n');
        else putc(' ');
    }
    if ((first + days) % 7 != 0) putc('\n');
}

/* ---------- Multiboot / CPU information ---------- */

static void parse_multiboot_info(uint32_t addr) {
    if (!addr) return;

    uint32_t total_size = *(uint32_t*)addr;
    uint32_t pos = addr + 8;
    uint32_t end = addr + total_size;

    while (pos + 8 <= end) {
        uint32_t type = *(uint32_t*)pos;
        uint32_t size = *(uint32_t*)(pos + 4);
        if (size < 8) break;

        if (type == 2 && size > 8) {
            bootloader_name = (const char*)(pos + 8);
        } else if (type == 4 && size >= 16) {
            mem_lower_kib = *(uint32_t*)(pos + 8);
            mem_upper_kib = *(uint32_t*)(pos + 12);
        }

        pos += (size + 7u) & ~7u;
    }
}

static void cpuid(uint32_t leaf, uint32_t* a, uint32_t* b,
                  uint32_t* c, uint32_t* d) {
    __asm__ volatile(
        "cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf)
    );
}

static void cpu_vendor(char* out, int len) {
    uint32_t a, b, c, d;
    cpuid(0, &a, &b, &c, &d);
    if (len < 13) return;
    out[0]  = (char)(b & 0xFF); out[1]  = (char)((b >> 8) & 0xFF);
    out[2]  = (char)((b >> 16) & 0xFF); out[3]  = (char)((b >> 24) & 0xFF);
    out[4]  = (char)(d & 0xFF); out[5] = (char)((d >> 8) & 0xFF);
    out[6]  = (char)((d >> 16) & 0xFF); out[7] = (char)((d >> 24) & 0xFF);
    out[8]  = (char)(c & 0xFF); out[9] = (char)((c >> 8) & 0xFF);
    out[10] = (char)((c >> 16) & 0xFF); out[11] = (char)((c >> 24) & 0xFF);
    out[12] = 0;
}

static uint8_t cpu_family(void) {
    uint32_t a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    uint32_t base = (a >> 8) & 0x0F;
    uint32_t ext  = (a >> 20) & 0xFF;
    return (uint8_t)(base + (base == 0x0F ? ext : 0));
}

static void cmd_sysinfo(void) {
    char vendor[13] = {0};
    cpu_vendor(vendor, sizeof(vendor));

    print_color("MyOS System Information\n", 0x0B);
    print("------------------------\n");
    print("OS:         MyOS v0.5\n");
    print("Kernel:     32-bit i386 / Multiboot2\n");
    print("CPU:        "); print(vendor); putc('\n');
    print("CPU family: "); print_uint(cpu_family()); putc('\n');
    print("RAM:        ");
    print_uint((mem_lower_kib + mem_upper_kib) / 1024u);
    print(" MiB (reported by Multiboot)\n");
    print("Bootloader: "); print(bootloader_name); putc('\n');
    print("Theme:      "); print(current_theme); putc('\n');
    print("Hostname:   "); print(hostname); putc('\n');
    print("Username:   "); print(username); putc('\n');
    print("Ticks:      "); print_uint(timer_ticks); putc('\n');
}

static void cmd_memory(void) {
    print("Lower memory: "); print_uint(mem_lower_kib); print(" KiB\n");
    print("Upper memory: "); print_uint(mem_upper_kib); print(" KiB\n");
    print("Total approx: ");
    print_uint((mem_lower_kib + mem_upper_kib) / 1024u);
    print(" MiB\n");
}

/* ---------- Task manager ---------- */

struct task_info {
    const char* name;
    const char* state;
    const char* kind;
};

static const struct task_info tasks[] = {
    { "kernel",   "RUNNING", "core" },
    { "shell",    "RUNNING", "service" },
    { "timer",    "ACTIVE",  "IRQ0" },
    { "keyboard", "ACTIVE",  "IRQ1" },
    { "rtc",      "READY",   "device" },
    { "logger",   "READY",   "service" },
    { "notify",   "READY",   "service" }
};

static void cmd_taskmgr(void) {
    print("MyOS Task Manager\n");
    print("-----------------\n");
    print("Name        State    Type\n");
    print("-----------------------------\n");
    for (size_t i = 0; i < sizeof(tasks) / sizeof(tasks[0]); ++i) {
        print(tasks[i].name);
        int pad = 12 - (int)str_len(tasks[i].name);
        while (pad-- > 0) putc(' ');
        print(tasks[i].state);
        int pad2 = 9 - (int)str_len(tasks[i].state);
        while (pad2-- > 0) putc(' ');
        print(tasks[i].kind);
        putc('\n');
    }
    print("\nNote: these are kernel services, not user processes yet.\n");
    print("Ticks: ");
    print_uint(timer_ticks);
    putc('\n');
}

/* ---------- Settings / profile ---------- */

static int valid_name(const char* s, int max_len) {
    if (!*s) return 0;
    int n = 0;
    while (*s) {
        char c = *s++;
        if (n++ >= max_len) return 0;
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return 0;
        }
    }
    return 1;
}

static void cmd_profile(void) {
    print("User Profile\n");
    print("------------\n");
    print("Username : "); print(username); putc('\n');
    print("Hostname : "); print(hostname); putc('\n');
    print("Theme    : "); print(current_theme); putc('\n');
}

static void cmd_settings(void) {
    print("MyOS Settings\n");
    print("-------------\n");
    print("username = "); print(username); putc('\n');
    print("hostname = "); print(hostname); putc('\n');
    print("theme    = "); print(current_theme); putc('\n');
    print("\nUse: set username=NAME\n");
    print("     set hostname=NAME\n");
    print("     theme matrix|ice|amber|mono\n");
}

static void cmd_set(const char* arg) {
    if (starts_with(arg, "username=")) {
        const char* value = arg + 9;
        if (!valid_name(value, 14)) {
            print("Invalid username. Use letters, numbers, _ or -.\n");
            return;
        }
        str_copy(username, value, sizeof(username));
        log_event("Username changed");
        notify_add("Username changed");
        print("Username updated.\n");
        return;
    }

    if (starts_with(arg, "hostname=")) {
        const char* value = arg + 9;
        if (!valid_name(value, 22)) {
            print("Invalid hostname. Use letters, numbers, _ or -.\n");
            return;
        }
        str_copy(hostname, value, sizeof(hostname));
        log_event("Hostname changed");
        notify_add("Hostname changed");
        print("Hostname updated.\n");
        return;
    }

    print("Usage: set username=NAME\n");
    print("       set hostname=NAME\n");
}

static void prompt(void) {
    print_color(username, 0x0A);
    print("> ");
}

/* ---------- Calculator ---------- */

struct calc_parser {
    const char* s;
    int error;
};

static void calc_spaces(struct calc_parser* p) {
    while (*p->s == ' ') ++p->s;
}

static int32_t calc_expr(struct calc_parser* p);

static int32_t calc_number(struct calc_parser* p) {
    calc_spaces(p);
    int32_t n = 0;
    int found = 0;

    while (*p->s >= '0' && *p->s <= '9') {
        found = 1;
        n = n * 10 + (*p->s - '0');
        ++p->s;
    }

    if (!found) p->error = 1;
    return n;
}

static int32_t calc_power(struct calc_parser* p) {
    calc_spaces(p);

    int neg = 0;
    if (*p->s == '-') {
        neg = 1;
        ++p->s;
    } else if (*p->s == '+') {
        ++p->s;
    }

    calc_spaces(p);
    int32_t value;

    if (*p->s == '(') {
        ++p->s;
        value = calc_expr(p);
        calc_spaces(p);
        if (*p->s != ')') {
            p->error = 1;
            return 0;
        }
        ++p->s;
    } else {
        value = calc_number(p);
    }

    if (neg) value = -value;

    calc_spaces(p);
    if (*p->s == '^') {
        ++p->s;
        int32_t exponent = calc_power(p);
        if (exponent < 0 || exponent > 31) {
            p->error = 1;
            return 0;
        }
        int32_t base = value;
        int32_t result = 1;
        for (int32_t i = 0; i < exponent; ++i) result *= base;
        value = result;
    }

    return value;
}

static int32_t calc_term(struct calc_parser* p) {
    int32_t result = calc_power(p);

    for (;;) {
        calc_spaces(p);
        char op = *p->s;
        if (op != '*' && op != '/' && op != '%') break;
        ++p->s;

        int32_t rhs = calc_power(p);
        if (p->error) return 0;

        if ((op == '/' || op == '%') && rhs == 0) {
            p->error = 1;
            return 0;
        }

        if (op == '*') result *= rhs;
        else if (op == '/') result /= rhs;
        else result %= rhs;
    }

    return result;
}

static int32_t calc_expr(struct calc_parser* p) {
    int32_t result = calc_term(p);

    for (;;) {
        calc_spaces(p);
        char op = *p->s;
        if (op != '+' && op != '-') break;
        ++p->s;

        int32_t rhs = calc_term(p);
        if (p->error) return 0;

        if (op == '+') result += rhs;
        else result -= rhs;
    }

    return result;
}

static void cmd_calc(const char* expr) {
    if (!*expr) {
        print("Usage: calc EXPRESSION\n");
        print("Example: calc (12+8)*3^2-5\n");
        return;
    }

    struct calc_parser p;
    p.s = expr;
    p.error = 0;

    int32_t result = calc_expr(&p);
    calc_spaces(&p);

    if (p.error || *p.s != 0) {
        print_color("Calculator error.\n", 0x0C);
        return;
    }

    print("Result: ");
    print_int(result);
    putc('\n');
}

/* ---------- history / line editing ---------- */

static void history_add(const char* text) {
    if (!*text) return;

    if (history_count > 0 &&
        streq(history[(history_count - 1) % HISTORY_COUNT], text)) {
        history_cursor = history_count;
        return;
    }

    int idx;
    if (history_count < HISTORY_COUNT) {
        idx = history_count++;
    } else {
        idx = history_count % HISTORY_COUNT;
    }
    str_copy(history[idx], text, LINE_MAX);
    history_cursor = history_count;
}

static const char* command_names[] = {
    "help","version","clear","ascii","echo","calc","theme",
    "history","profile","whoami","hostname","sysinfo","mem",
    "time","clock","date","calendar","taskmgr","settings","set",
    "notify","notifications","logs","net","ping","uptime",
    "reboot","shutdown"
};

static void erase_line_visual(void) {
    for (int i = 0; i < cursor_pos; ++i) putc('\b');
    for (int i = 0; i < line_len; ++i) putc(' ');
    for (int i = 0; i < line_len; ++i) putc('\b');
    cursor_pos = 0;
}

static void redraw_line(void) {
    erase_line_visual();
    for (int i = 0; i < line_len; ++i) putc(line[i]);
    cursor_pos = line_len;
}

static void set_line(const char* text) {
    str_copy(line, text, LINE_MAX);
    line_len = (int)str_len(line);
    cursor_pos = line_len;
    redraw_line();
}

static void cmd_history(void) {
    if (history_count == 0) {
        print("History is empty.\n");
        return;
    }
    print("Command history:\n");
    int start = (history_count < HISTORY_COUNT) ? 0 : history_count % HISTORY_COUNT;
    int shown = (history_count < HISTORY_COUNT) ? history_count : HISTORY_COUNT;

    for (int i = 0; i < shown; ++i) {
        int idx = (start + i) % HISTORY_COUNT;
        print("  ");
        print_uint((uint32_t)(i + 1));
        print("  ");
        print(history[idx]);
        putc('\n');
    }
}

static int first_token_len(const char* s) {
    int n = 0;
    while (s[n] && s[n] != ' ') ++n;
    return n;
}

static void cmd_complete(void) {
    if (line_len == 0) {
        set_line("help");
        return;
    }

    int n = first_token_len(line);
    if (n == 0 || n >= LINE_MAX) return;

    int matches = 0;
    const char* match = 0;

    for (size_t i = 0; i < sizeof(command_names) / sizeof(command_names[0]); ++i) {
        int ok = 1;
        for (int j = 0; j < n; ++j) {
            if (command_names[i][j] != line[j]) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            ++matches;
            match = command_names[i];
        }
    }

    if (matches == 1 && n == line_len) {
        set_line(match);
    } else if (matches > 1) {
        putc('\n');
        for (size_t i = 0; i < sizeof(command_names) / sizeof(command_names[0]); ++i) {
            int ok = 1;
            for (int j = 0; j < n; ++j) {
                if (command_names[i][j] != line[j]) ok = 0;
            }
            if (ok) {
                print(command_names[i]);
                print("  ");
            }
        }
        putc('\n');
        prompt();
        for (int i = 0; i < line_len; ++i) putc(line[i]);
        cursor_pos = line_len;
    }
}

/* ---------- network tools: PCI discovery + status ---------- */

static uint32_t pci_config_read32(uint8_t bus, uint8_t slot,
                                  uint8_t func, uint8_t offset) {
    uint32_t address =
        0x80000000u |
        ((uint32_t)bus << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)func << 8) |
        (offset & 0xFCu);

    outl(0xCF8, address);
    return inl(0xCFC);
}

static int pci_find_network(uint16_t* vendor, uint16_t* device) {
    for (uint16_t bus = 0; bus < 256; ++bus) {
        for (uint8_t slot = 0; slot < 32; ++slot) {
            for (uint8_t func = 0; func < 8; ++func) {
                uint32_t id = pci_config_read32((uint8_t)bus, slot, func, 0x00);
                uint16_t v = (uint16_t)(id & 0xFFFFu);
                uint16_t d = (uint16_t)((id >> 16) & 0xFFFFu);
                if (v == 0xFFFFu) continue;

                uint32_t class_reg = pci_config_read32((uint8_t)bus, slot, func, 0x08);
                uint8_t class_code = (uint8_t)((class_reg >> 24) & 0xFFu);

                if (class_code == 0x02) {
                    *vendor = v;
                    *device = d;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void cmd_net(void) {
    uint16_t vendor = 0, device = 0;

    print("MyOS Network Status\n");
    print("-------------------\n");
    if (pci_find_network(&vendor, &device)) {
        print("NIC:        detected\n");
        print("Vendor ID:  0x");
        print_hex16(vendor);
        putc('\n');
        print("Device ID:  0x");
        print_hex16(device);
        putc('\n');
        print("Link:       not queried (driver pending)\n");
    } else {
        print("NIC:        not detected via PCI\n");
    }

    print("IP:         0.0.0.0\n");
    print("DHCP:       not configured\n");
    print("TCP/IP:     kernel stack not loaded\n");
    print("Ping:       driver/stack required\n");
}

static void cmd_ping(const char* host) {
    if (!*host) {
        print("Usage: ping HOST\n");
        return;
    }

    print("ping ");
    print(host);
    print(": network driver and TCP/IP stack are not enabled yet.\n");
    print("NIC discovery is available with: net\n");
}

/* ---------- help ---------- */

static void print_command_help(const char* name) {
    if (streq(name, "calc")) {
        print("calc EXPR - integer calculator (+ - * / % ^, parentheses)\n");
    } else if (streq(name, "theme")) {
        print("theme NAME - change terminal theme: matrix, ice, amber, mono\n");
    } else if (streq(name, "history")) {
        print("history - show recent commands; Up/Down navigates them\n");
    } else if (streq(name, "sysinfo")) {
        print("sysinfo - CPU, RAM, bootloader, profile and kernel information\n");
    } else if (streq(name, "clock")) {
        print("clock - live RTC clock; press any key to exit\n");
    } else if (streq(name, "calendar")) {
        print("calendar - show the current month\n");
    } else if (streq(name, "taskmgr")) {
        print("taskmgr - show active MyOS kernel services\n");
    } else if (streq(name, "settings")) {
        print("settings - show current MyOS settings\n");
    } else if (streq(name, "set")) {
        print("set username=NAME | hostname=NAME\n");
    } else if (streq(name, "notify")) {
        print("notify MESSAGE - create a system notification\n");
    } else if (streq(name, "notifications")) {
        print("notifications - view notifications; notifications clear removes them\n");
    } else if (streq(name, "logs")) {
        print("logs - view system logs; logs clear clears them\n");
    } else if (streq(name, "net")) {
        print("net - detect PCI network controller and show network status\n");
    } else if (streq(name, "ping")) {
        print("ping HOST - command shell for the future network stack\n");
    } else {
        print("No detailed help for that command.\n");
    }
}

static void cmd_help(const char* topic) {
    if (*topic) {
        if (streq(topic, "shell")) {
            print("Shell: history, tab completion, set, settings, profile, whoami, hostname, theme\n");
        } else if (streq(topic, "system")) {
            print("System: sysinfo, mem, uptime, time, clock, date, calendar, taskmgr, logs, notifications\n");
        } else if (streq(topic, "network")) {
            print("Network: net, ping\n");
        } else if (streq(topic, "all")) {
            print("Commands:\n");
            print(" help version clear ascii echo calc theme history profile\n");
            print(" whoami hostname sysinfo mem time clock date calendar taskmgr\n");
            print(" settings set notify notifications logs net ping uptime reboot shutdown\n");
        } else {
            print_command_help(topic);
        }
        return;
    }

    print_color("MyOS Help Center\n", 0x0B);
    print("----------------\n");
    print("Shell:    history  completion(Tab)  profile  settings  set  theme\n");
    print("Utility:  calc      time  clock  date  calendar  sysinfo  mem\n");
    print("System:   taskmgr   logs  notifications  uptime\n");
    print("Network:  net       ping\n");
    print("Other:    version   clear  ascii  echo  reboot  shutdown\n");
    print("\nTopics: help shell | help system | help network | help all\n");
}

/* ---------- command execution ---------- */

static void command_arg_after(const char* cmd, int prefix_len, const char** out) {
    const char* p = cmd + prefix_len;
    while (*p == ' ') ++p;
    *out = p;
}

static void execute(const char* cmd) {
    while (*cmd == ' ') ++cmd;
    if (!*cmd) return;

    log_event("Command executed");

    if (streq(cmd, "help")) cmd_help("");
    else if (starts_with(cmd, "help ")) {
        const char* arg;
        command_arg_after(cmd, 5, &arg);
        cmd_help(arg);
    }
    else if (streq(cmd, "version")) {
        print("MyOS v0.5 \"Terminal\"\n");
        print("32-bit i386 | Multiboot2 | GitHub Actions\n");
    }
    else if (streq(cmd, "clear") || streq(cmd, "cls")) {
        clear_screen();
        draw_banner();
    }
    else if (streq(cmd, "ascii")) {
        draw_banner();
    }
    else if (streq(cmd, "echo")) {
        putc('\n');
    }
    else if (starts_with(cmd, "echo ")) {
        const char* arg;
        command_arg_after(cmd, 5, &arg);
        print(arg);
        putc('\n');
    }
    else if (streq(cmd, "calc")) cmd_calc("");
    else if (starts_with(cmd, "calc ")) {
        const char* arg;
        command_arg_after(cmd, 5, &arg);
        cmd_calc(arg);
    }
    else if (streq(cmd, "theme")) cmd_theme("");
    else if (starts_with(cmd, "theme ")) {
        const char* arg;
        command_arg_after(cmd, 6, &arg);
        cmd_theme(arg);
    }
    else if (streq(cmd, "history")) cmd_history();
    else if (streq(cmd, "profile") || streq(cmd, "whoami")) cmd_profile();
    else if (streq(cmd, "hostname")) {
        print("Hostname: ");
        print(hostname);
        putc('\n');
    }
    else if (streq(cmd, "sysinfo")) cmd_sysinfo();
    else if (streq(cmd, "mem")) cmd_memory();
    else if (streq(cmd, "uptime")) {
        print("Timer ticks: ");
        print_uint(timer_ticks);
        print("\nApprox. seconds: ");
        print_uint(timer_ticks / 100u);
        putc('\n');
    }
    else if (streq(cmd, "time") || streq(cmd, "date")) cmd_time();
    else if (streq(cmd, "clock")) cmd_clock();
    else if (streq(cmd, "calendar")) cmd_calendar();
    else if (streq(cmd, "taskmgr")) cmd_taskmgr();
    else if (streq(cmd, "settings")) cmd_settings();
    else if (starts_with(cmd, "set ")) {
        const char* arg;
        command_arg_after(cmd, 4, &arg);
        cmd_set(arg);
    }
    else if (streq(cmd, "set")) cmd_set("");
    else if (starts_with(cmd, "notify ")) {
        const char* arg;
        command_arg_after(cmd, 7, &arg);
        cmd_notify(arg);
    }
    else if (streq(cmd, "notify")) cmd_notify("");
    else if (streq(cmd, "notifications")) cmd_notifications("");
    else if (streq(cmd, "notifications clear")) cmd_notifications("clear");
    else if (streq(cmd, "logs")) cmd_logs("");
    else if (streq(cmd, "logs clear")) cmd_logs("clear");
    else if (streq(cmd, "net")) cmd_net();
    else if (starts_with(cmd, "ping ")) {
        const char* arg;
        command_arg_after(cmd, 5, &arg);
        cmd_ping(arg);
    }
    else if (streq(cmd, "ping")) cmd_ping("");
    else if (streq(cmd, "reboot")) {
        print_color("\n[*] Rebooting MyOS...\n", 0x0E);
        cpu_cli();
        for (volatile uint32_t i = 0; i < 100000u; ++i) {}
        outb(0x64, 0xFE);
        outb(0xCF9, 0x06);
        for (;;) cpu_halt();
    }
    else if (streq(cmd, "shutdown")) {
        print_color("\n[*] Sending power-off request...\n", 0x0E);
        notify_add("Shutdown requested");
        log_event("Shutdown requested");
        cpu_cli();

        /* Common soft-off values supported by several PC emulators.
         * If the hypervisor ignores these ports, halt the guest safely.
         */
        outw(0x604, 0x2000);
        outw(0xB004, 0x2000);

        for (;;) cpu_halt();
    }
    else {
        print_color("Unknown command: ", 0x0C);
        print(cmd);
        putc('\n');
        print("Type 'help' for commands.\n");
    }
}

/* ---------- keyboard ---------- */

static const char kbd_map[128] = {
    [0x02]='1',[0x03]='2',[0x04]='3',[0x05]='4',[0x06]='5',
    [0x07]='6',[0x08]='7',[0x09]='8',[0x0A]='9',[0x0B]='0',
    [0x0C]='-',[0x0D]='=',[0x0E]='\b',[0x0F]='\t',
    [0x10]='q',[0x11]='w',[0x12]='e',[0x13]='r',[0x14]='t',
    [0x15]='y',[0x16]='u',[0x17]='i',[0x18]='o',[0x19]='p',
    [0x1A]='[',[0x1B]=']',[0x1C]='\n',
    [0x1E]='a',[0x1F]='s',[0x20]='d',[0x21]='f',[0x22]='g',
    [0x23]='h',[0x24]='j',[0x25]='k',[0x26]='l',[0x27]=';',
    [0x28]='\'',[0x29]='`',[0x2B]='\\',[0x2C]='z',[0x2D]='x',
    [0x2E]='c',[0x2F]='v',[0x30]='b',[0x31]='n',[0x32]='m',
    [0x33]=',',[0x34]='.',[0x35]='/',[0x39]=' '
};

static const char kbd_shift_map[128] = {
    [0x02]='!',[0x03]='@',[0x04]='#',[0x05]='$',[0x06]='%',
    [0x07]='^',[0x08]='&',[0x09]='*',[0x0A]='(',[0x0B]=')',
    [0x0C]='_',[0x0D]='+',[0x1A]='{',[0x1B]='}',[0x27]=':',
    [0x28]='"',[0x29]='~',[0x2B]='|',[0x33]='<',[0x34]='>',
    [0x35]='?'
};

static void kbd_push(uint8_t event) {
    uint16_t next = (uint16_t)((kbd_head + 1) % KBD_BUF_SIZE);
    if (next == kbd_tail) return;
    kbd_buf[kbd_head] = event;
    kbd_head = next;
}

static char kbd_translate(uint8_t sc) {
    char c = kbd_map[sc];
    if (!c) return 0;

    if (c >= 'a' && c <= 'z') {
        if (caps_lock ^ shift_down)
            return (char)(c - 'a' + 'A');
        return c;
    }

    if (shift_down && kbd_shift_map[sc])
        return kbd_shift_map[sc];

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
        if (!(sc & 0x80u)) {
            uint8_t code = sc & 0x7Fu;
            if (code == 0x48) kbd_push(KEY_UP);
            else if (code == 0x50) kbd_push(KEY_DOWN);
            else if (code == 0x4B) kbd_push(KEY_LEFT);
            else if (code == 0x4D) kbd_push(KEY_RIGHT);
            else if (code == 0x53) kbd_push(KEY_DELETE);
        }
        pic_eoi(1);
        return;
    }

    uint8_t released = (uint8_t)(sc & 0x80u);
    uint8_t code = (uint8_t)(sc & 0x7Fu);

    if (code == 0x2A || code == 0x36) {
        shift_down = (uint8_t)!released;
        pic_eoi(1);
        return;
    }

    if (released) {
        pic_eoi(1);
        return;
    }

    if (code == 0x3A) {
        caps_lock = (uint8_t)!caps_lock;
        return;
    }

    char c = kbd_translate(code);
    if (c) kbd_push((uint8_t)c);

    pic_eoi(1);
}

/* ---------- interrupts ---------- */

static void idt_set_gate(int n, uint32_t handler) {
    if (n < 0 || n >= IDT_ENTRIES) return;
    idt[n].base_lo = (uint16_t)(handler & 0xFFFFu);
    idt[n].sel = 0x08;
    idt[n].always0 = 0;
    idt[n].flags = 0x8E;
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

    outb(PIC1_DATA, 0xFC); /* IRQ0 + IRQ1 */
    outb(PIC2_DATA, 0xFF);
}

static void pic_eoi(int irq) {
    if (irq >= 8) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

static void pit_init(uint32_t frequency_hz) {
    if (frequency_hz == 0) return;
    uint32_t divisor = 1193180u / frequency_hz;
    if (divisor == 0 || divisor > 0xFFFFu) return;

    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

/* ---------- exception handling ---------- */

static const char* const exception_names[32] = {
    "Division by zero","Debug","Non-maskable interrupt","Breakpoint",
    "Overflow","Bound range exceeded","Invalid opcode","Device not available",
    "Double fault","Coprocessor segment overrun","Invalid TSS","Segment not present",
    "Stack-segment fault","General protection fault","Page fault","Reserved",
    "x87 floating-point","Alignment check","Machine check","SIMD floating-point",
    "Virtualization","Control protection","Reserved","Reserved",
    "Reserved","Reserved","Reserved","Hypervisor injection",
    "VMM communication","Security exception","Reserved"
};

void exception_handler(uint32_t interrupt_number) {
    cpu_cli();
    print_color("\n\n!!! KERNEL EXCEPTION !!!\n", 0x0C);
    if (interrupt_number < 32)
        print_color(exception_names[interrupt_number], 0x0E);
    else
        print_color("Unknown exception", 0x0E);

    print(" (#");
    print_uint(interrupt_number);
    print(")\nSystem halted.\n");
    log_event("Kernel exception");
    for (;;) cpu_halt();
}

/* ---------- banner / boot ---------- */

static void draw_banner(void) {
    print_color("  __  __       ___  ____  \n", 0x0B);
    print_color(" |  \\/  |_   _/ _ \\/ ___| \n", 0x0B);
    print_color(" | |\\/| | | | | | | \\___ \\ \n", 0x0B);
    print_color(" | |  | | |_| | |_| |___) |\n", 0x0B);
    print_color(" |_|  |_|\\__, |\\___/|____/ \n", 0x0B);
    print_color("         |___/             \n", 0x0B);
    print_color("        Terminal v0.5\n", 0x08);
    print_color("----------------------------------------\n\n", 0x08);
}

void kernel_main(uint32_t magic, void* mb_info) {
    clear_screen();

    if (magic != 0x36D76289u) {
        print_color("Bad Multiboot2 magic: ", 0x0C);
        print_uint(magic);
        print("\nSystem halted.\n");
        cpu_cli();
        for (;;) cpu_halt();
    }

    parse_multiboot_info((uint32_t)mb_info);
    idt_init();
    pic_remap();
    pit_init(100u);

    draw_banner();
    print("Welcome to ");
    print_color("MyOS", 0x0A);
    print(". Type 'help' for the Help Center.\n");
    print("Try: calc (12+8)*3^2 | theme ice | sysinfo | taskmgr\n\n");

    log_event("MyOS boot complete");
    notify_add("MyOS boot completed");
    cpu_sti();

    prompt();

    for (;;) {
        int event;
        while ((event = kbd_get_event()) >= 0) {
            if (event == KEY_UP) {
                if (history_count > 0 && history_cursor > 0) {
                    --history_cursor;
                    set_line(history[history_cursor % HISTORY_COUNT]);
                }
            } else if (event == KEY_DOWN) {
                if (history_cursor < history_count - 1) {
                    ++history_cursor;
                    set_line(history[history_cursor % HISTORY_COUNT]);
                } else {
                    history_cursor = history_count;
                    set_line("");
                }
            } else if (event == KEY_LEFT) {
                if (cursor_pos > 0) {
                    putc('\b');
                    --cursor_pos;
                }
            } else if (event == KEY_RIGHT) {
                if (cursor_pos < line_len) {
                    putc(line[cursor_pos]);
                    ++cursor_pos;
                }
            } else if (event == KEY_DELETE) {
                if (cursor_pos < line_len) {
                    for (int i = cursor_pos; i < line_len - 1; ++i)
                        line[i] = line[i + 1];
                    --line_len;
                    redraw_line();
                }
            } else if (event == '\n') {
                putc('\n');
                line[line_len] = 0;
                trim_spaces(line);
                history_add(line);
                execute(line);
                line_len = 0;
                cursor_pos = 0;
                line[0] = 0;
                prompt();
            } else if (event == '\b') {
                if (cursor_pos > 0) {
                    for (int i = cursor_pos - 1; i < line_len - 1; ++i)
                        line[i] = line[i + 1];
                    --line_len;
                    --cursor_pos;
                    redraw_line();
                }
            } else if (event == '\t') {
                cmd_complete();
            } else if (event >= 32 && event < 127) {
                if (line_len < LINE_MAX - 1) {
                    for (int i = line_len; i > cursor_pos; --i)
                        line[i] = line[i - 1];
                    line[cursor_pos++] = (char)event;
                    ++line_len;
                    redraw_line();
                }
            }
        }

        cpu_halt();
    }
}
