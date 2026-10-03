#include <stdint.h>
#include <stddef.h>
#include "graphics/framebuffer.h"
#include "graphics/renderer.h"
#include "gui/gui.h"
#include "drivers/mouse.h"
#include "fs/fs.h"
#include "net/net.h"

/* ============================================================
 * MyOS v0.5 - desktop-style operating environment
 * Features:
 *   calculator, themes, history, tab completion, profile,
 *   system info, RTC clock/calendar, task manager,
 *   help center, settings, notifications, logs, network tools.
 *   GUI: framebuffer desktop, mouse input, graphical terminal shell.
 * ============================================================ */

#define VGA_WIDTH       80
#define VGA_HEIGHT      25
#define VGA_CRTC_INDEX   0x3D4
#define VGA_CRTC_DATA    0x3D5
#define VGA_CELLS        (VGA_WIDTH * VGA_HEIGHT)
#ifdef UNIT_TEST
static volatile uint16_t unit_test_vga[VGA_CELLS];
#define VGA_MEMORY ((uintptr_t)unit_test_vga)
#else
#define VGA_MEMORY      0xB8000
#endif
#define IDT_ENTRIES     256
#define KBD_BUF_SIZE    256
#define LINE_MAX        72
#define HISTORY_COUNT   16
#define LOG_COUNT       32
#define LOG_LEN         96
#define PROCESS_MAX     8
#define SCHED_QUANTUM   5
#define SCHED_SAMPLE    100
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
#define KEY_HOME        0x85
#define KEY_END         0x86
#define KEY_PAGEUP      0x87
#define KEY_PAGEDOWN    0x88
#define KEY_SHIFT_LEFT  0x89
#define KEY_SHIFT_RIGHT 0x8A
#define KEY_CTRL_L      0x8B
#define KEY_ESCAPE      0x8C

static volatile uint16_t* const vga = (volatile uint16_t*)VGA_MEMORY;
static int row = 0;
static int col = 0;
static uint8_t color = 0x0A;
static uint16_t line_origin_cell = 0;
static int line_rendered_len = 0;
static int gui_console_enabled = 0;

static volatile uint8_t kbd_buf[KBD_BUF_SIZE];
static volatile uint16_t kbd_head = 0;
static volatile uint16_t kbd_tail = 0;

static volatile uint32_t timer_ticks = 0;

enum process_state {
    PROCESS_READY = 0,
    PROCESS_RUNNING = 1,
    PROCESS_SLEEPING = 2,
    PROCESS_BLOCKED = 3,
    PROCESS_TERMINATED = 4
};

struct kernel_process {
    uint32_t pid;
    char name[20];
    enum process_state state;
    uint32_t cpu_ticks;
    uint32_t sample_cpu_ticks;
    uint32_t runs;
    uint16_t memory_kib;
    uint8_t priority;
    uint8_t cpu_percent;
};

static struct kernel_process processes[PROCESS_MAX];
static int process_count;
static int scheduler_current = -1;
static uint32_t scheduler_slice_ticks;
static uint32_t scheduler_sample_tick;
static uint32_t next_pid = 1;
static volatile uint8_t shift_down = 0;
static volatile uint8_t ctrl_down = 0;
static volatile uint8_t caps_lock = 0;
static volatile uint8_t extended_scancode = 0;

static char line[LINE_MAX];
static int line_len = 0;
static int cursor_pos = 0;
static int selection_anchor = -1;
static char clipboard[LINE_MAX];
static int clipboard_len;

static char history[HISTORY_COUNT][LINE_MAX];
static int history_count = 0;
static int history_head = 0;
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
extern void irq12_stub(void);

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

#ifdef UNIT_TEST
static uint32_t unit_test_ports[65536];

static inline void outb(uint16_t port, uint8_t val) {
    unit_test_ports[port] = (unit_test_ports[port] & 0xFFFFFF00u) | val;
}

static inline uint8_t inb(uint16_t port) {
    return (uint8_t)(unit_test_ports[port] & 0xFFu);
}

static inline uint16_t inw(uint16_t port) {
    return (uint16_t)(unit_test_ports[port] & 0xFFFFu);
}

static inline void outw(uint16_t port, uint16_t val) {
    unit_test_ports[port] = (unit_test_ports[port] & 0xFFFF0000u) | val;
}

static inline void outl(uint16_t port, uint32_t val) {
    unit_test_ports[port] = val;
}

static inline uint32_t inl(uint16_t port) {
    return unit_test_ports[port];
}
#else
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
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
#endif

static void debug_putc(char c) {
    outb(0xE9, (uint8_t)c);
}

static void debug_write(const char* s) {
    if (!s) return;
    while (*s) debug_putc(*s++);
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

static const char* process_state_name(enum process_state state) {
    if (state == PROCESS_RUNNING) return "RUNNING";
    if (state == PROCESS_SLEEPING) return "SLEEPING";
    if (state == PROCESS_BLOCKED) return "BLOCKED";
    if (state == PROCESS_TERMINATED) return "TERMINATED";
    return "READY";
}

static int process_find(uint32_t pid) {
    for (int i = 0; i < process_count; ++i)
        if (processes[i].pid == pid) return i;
    return -1;
}

static int process_add(const char* name, enum process_state state,
                       uint16_t memory_kib, uint8_t priority) {
    if (process_count >= PROCESS_MAX || !name) return -1;
    int i = process_count++;
    processes[i].pid = next_pid++;
    str_copy(processes[i].name, name, (int)sizeof(processes[i].name));
    processes[i].state = state;
    processes[i].cpu_ticks = 0;
    processes[i].sample_cpu_ticks = 0;
    processes[i].runs = 0;
    processes[i].memory_kib = memory_kib;
    processes[i].priority = priority;
    processes[i].cpu_percent = 0;
    return i;
}

static int scheduler_choose_next(void) {
    if (process_count <= 0) return -1;
    for (int offset = 1; offset <= process_count; ++offset) {
        int i = (scheduler_current + offset + process_count) % process_count;
        if (processes[i].state == PROCESS_READY) return i;
    }
    for (int i = 0; i < process_count; ++i)
        if (processes[i].state == PROCESS_READY) return i;
    return -1;
}

static void scheduler_switch(void) {
    if (scheduler_current >= 0 && scheduler_current < process_count &&
        processes[scheduler_current].state == PROCESS_RUNNING)
        processes[scheduler_current].state = PROCESS_READY;

    int next = scheduler_choose_next();
    scheduler_current = next;
    scheduler_slice_ticks = 0;
    if (next >= 0) {
        processes[next].state = PROCESS_RUNNING;
        ++processes[next].runs;
    }
}

static void scheduler_tick(void) {
    if (process_count <= 0) return;

    if (scheduler_current < 0 || scheduler_current >= process_count ||
        processes[scheduler_current].state != PROCESS_RUNNING)
        scheduler_switch();

    if (scheduler_current >= 0 && processes[scheduler_current].state == PROCESS_RUNNING) {
        ++processes[scheduler_current].cpu_ticks;
        ++scheduler_slice_ticks;
        if (scheduler_slice_ticks >= SCHED_QUANTUM) scheduler_switch();
    }

    if ((uint32_t)(timer_ticks - scheduler_sample_tick) >= SCHED_SAMPLE) {
        scheduler_sample_tick = timer_ticks;
        for (int i = 0; i < process_count; ++i) {
            uint32_t delta = processes[i].cpu_ticks - processes[i].sample_cpu_ticks;
            processes[i].cpu_percent = (uint8_t)(delta > 100u ? 100u : delta);
            processes[i].sample_cpu_ticks = processes[i].cpu_ticks;
        }
    }
}

static void scheduler_init(void) {
    process_count = 0;
    scheduler_current = -1;
    scheduler_slice_ticks = 0;
    scheduler_sample_tick = 0;
    next_pid = 1;
    process_add("kernel", PROCESS_READY, 128, 5);
    process_add("terminal", PROCESS_READY, 64, 4);
    process_add("monitor", PROCESS_READY, 48, 3);
    process_add("calculator", PROCESS_READY, 36, 3);
    process_add("settings", PROCESS_READY, 32, 2);
    process_add("logger", PROCESS_READY, 24, 2);
    process_add("notifier", PROCESS_READY, 24, 2);
    process_add("rtc", PROCESS_READY, 16, 1);
    scheduler_switch();
}

static int scheduler_control(uint32_t pid, int terminate) {
    int i = process_find(pid);
    if (i < 0 || processes[i].pid == 1u) return 0;
    if (terminate) {
        processes[i].state = PROCESS_TERMINATED;
        if (i == scheduler_current) scheduler_switch();
    } else {
        processes[i].state = PROCESS_READY;
        processes[i].cpu_percent = 0;
        if (scheduler_current < 0) scheduler_switch();
    }
    return 1;
}

static void scheduler_gui_update(void) {
    gui_task_info_t snapshot[PROCESS_MAX];
    int count = process_count;
    if (count > PROCESS_MAX) count = PROCESS_MAX;
    for (int i = 0; i < count; ++i) {
        snapshot[i].pid = (int)processes[i].pid;
        snapshot[i].name = processes[i].name;
        snapshot[i].state = process_state_name(processes[i].state);
        snapshot[i].cpu_percent = (int)processes[i].cpu_percent;
        snapshot[i].memory_kib = (int)processes[i].memory_kib;
        snapshot[i].priority = (int)processes[i].priority;
    }
    gui_taskmgr_set_data(snapshot, count);
}



static void print(const char* s);
static void draw_banner(void);
static void prompt(void);
static void line_editor_set_cursor(void);
static void redraw_line(void);
static void pic_eoi(int irq);

static void vga_cursor_set(uint16_t position) {
    if (position >= VGA_CELLS) position = VGA_CELLS - 1;
    outb(VGA_CRTC_INDEX, 0x0F);
    outb(VGA_CRTC_DATA, (uint8_t)(position & 0xFFu));
    outb(VGA_CRTC_INDEX, 0x0E);
    outb(VGA_CRTC_DATA, (uint8_t)((position >> 8) & 0xFFu));
}

static void vga_cursor_init(void) {
    /* Standard VGA text cursor: enabled, full-height block. */
    outb(VGA_CRTC_INDEX, 0x0A);
    outb(VGA_CRTC_DATA, 0x06);
    outb(VGA_CRTC_INDEX, 0x0B);
    outb(VGA_CRTC_DATA, 0x0F);
    vga_cursor_set(0);
}

static void clear_screen(void) {
    if (gui_console_enabled && gui_available()) {
        gui_terminal_clear();
        row = 0;
        col = 0;
        line_rendered_len = 0;
        return;
    }
    for (int i = 0; i < VGA_CELLS; ++i) {
        vga[i] = ((uint16_t)color << 8) | ' ';
    }
    row = 0;
    col = 0;
    vga_cursor_set(0);
}

static void scroll(void) {
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; ++i) {
        vga[i] = vga[i + VGA_WIDTH];
    }
    for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_CELLS; ++i) {
        vga[i] = ((uint16_t)color << 8) | ' ';
    }
    row = VGA_HEIGHT - 1;
    vga_cursor_set((uint16_t)(row * VGA_WIDTH + col));
}

static void putc(char c) {
    if (gui_console_enabled && gui_available()) {
        gui_terminal_putchar(c, color);
        return;
    }
    if (c == '\n') {
        col = 0;
        ++row;
    } else if (c == '\r') {
        col = 0;
        vga_cursor_set((uint16_t)(row * VGA_WIDTH));
        return;
    } else if (c == '\b') {
        if (col > 0) {
            --col;
        } else if (row > 0) {
            --row;
            col = VGA_WIDTH - 1;
        }
        vga[row * VGA_WIDTH + col] = ((uint16_t)color << 8) | ' ';
        vga_cursor_set((uint16_t)(row * VGA_WIDTH + col));
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
    else vga_cursor_set((uint16_t)(row * VGA_WIDTH + col));
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
    uint32_t magnitude;
    if (value < 0) {
        putc('-');
        magnitude = (uint32_t)(-(value + 1));
        ++magnitude;
    } else {
        magnitude = (uint32_t)value;
    }
    print_uint(magnitude);
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
    { "mono",   0x0F, 0x00 },
    { "light",  0x00, 0x0F }
};

static void apply_theme(const struct theme_def* t) {
    color = (uint8_t)((t->bg << 4) | (t->fg & 0x0F));
    current_theme = t->name;
    if (gui_console_enabled && gui_available()) gui_set_theme(t->name);
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
        print("\nAvailable themes: matrix ice amber mono light\n");
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

    print("Unknown theme. Use: matrix, ice, amber, mono, light\n");
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

static int rtc_ready(void) {
    for (uint32_t i = 0; i < 10000u; ++i) {
        if (!(rtc_read_reg(0x0A) & 0x80u)) return 1;
    }
    return 0;
}

static int rtc_read(struct rtc_time* t) {
    if (!rtc_ready()) return 0;

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
    if (month < 1 || month > 12 || day < 1 || day > (uint8_t)days_in_month(full_year, month))
        return 0;

    t->year = full_year;
    t->weekday = weekday_calc(full_year, month, day);
    return 1;
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
    if (!rtc_read(&t)) {
        print_color("RTC read failed.\n", 0x0C);
        return;
    }
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
            if (!rtc_read(&t)) {
                print("\rTime unavailable       ");
                continue;
            }
            print("\rTime ");
            print_time_value(&t);
            print("        ");
        }
        cpu_halt();
    }
}

static void cmd_calendar(void) {
    struct rtc_time t;
    if (!rtc_read(&t)) {
        print_color("RTC read failed.\n", 0x0C);
        return;
    }
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
    if (total_size < 16u || total_size > 0x01000000u) return;
    if (addr > 0xFFFFFFFFu - total_size) return;

    uint32_t pos = addr + 8u;
    uint32_t end = addr + total_size;

    while (pos <= end && end - pos >= 8u) {
        uint32_t type = *(uint32_t*)pos;
        uint32_t size = *(uint32_t*)(pos + 4);
        if (size < 8u || size > end - pos) break;

        if (type == 2 && size > 8) {
            bootloader_name = (const char*)(pos + 8);
        } else if (type == 4 && size >= 16) {
            mem_lower_kib = *(uint32_t*)(pos + 8);
            mem_upper_kib = *(uint32_t*)(pos + 12);
        } else if (type == 8 && size >= 38) {
            uint64_t address = *(uint64_t*)(pos + 8);
            uint32_t pitch = *(uint32_t*)(pos + 16);
            uint32_t width = *(uint32_t*)(pos + 20);
            uint32_t height = *(uint32_t*)(pos + 24);
            uint8_t bpp = *(uint8_t*)(pos + 28);
            uint8_t fb_type = *(uint8_t*)(pos + 29);
            if (fb_type == 1u) {
                uint8_t red_pos = *(uint8_t*)(pos + 32);
                uint8_t red_mask = *(uint8_t*)(pos + 33);
                uint8_t green_pos = *(uint8_t*)(pos + 34);
                uint8_t green_mask = *(uint8_t*)(pos + 35);
                uint8_t blue_pos = *(uint8_t*)(pos + 36);
                uint8_t blue_mask = *(uint8_t*)(pos + 37);
                framebuffer_set_info(address, pitch, width, height, bpp, fb_type,
                                     red_pos, red_mask, green_pos, green_mask,
                                     blue_pos, blue_mask);
            }
        }

        uint32_t next = (size + 7u) & ~7u;
        if (next < size || next > end - pos) break;
        pos += next;
    }
}

#ifdef UNIT_TEST
static int cpuid_supported(void) {
    return 1;
}
#else
static int cpuid_supported(void) {
    uint32_t before, after;
    __asm__ volatile(
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl $0x200000, %1\n\t"
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"
        "pushl %0\n\t"
        "popfl"
        : "=r"(before), "=r"(after)
        :
        : "cc"
    );
    return ((before ^ after) & 0x200000u) != 0;
}
#endif

static void cpuid(uint32_t leaf, uint32_t* a, uint32_t* b,
                  uint32_t* c, uint32_t* d) {
    __asm__ volatile(
        "cpuid"
        : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "a"(leaf)
    );
}

static uint32_t cpuid_max_leaf(void) {
    uint32_t a, b, c, d;
    cpuid(0, &a, &b, &c, &d);
    return a;
}

static int cpu_vendor(char* out, int len) {
    uint32_t a, b, c, d;
    if (len < 13 || !cpuid_supported()) return 0;
    cpuid(0, &a, &b, &c, &d);
    out[0]  = (char)(b & 0xFF); out[1]  = (char)((b >> 8) & 0xFF);
    out[2]  = (char)((b >> 16) & 0xFF); out[3]  = (char)((b >> 24) & 0xFF);
    out[4]  = (char)(d & 0xFF); out[5] = (char)((d >> 8) & 0xFF);
    out[6]  = (char)((d >> 16) & 0xFF); out[7] = (char)((d >> 24) & 0xFF);
    out[8]  = (char)(c & 0xFF); out[9] = (char)((c >> 8) & 0xFF);
    out[10] = (char)((c >> 16) & 0xFF); out[11] = (char)((c >> 24) & 0xFF);
    out[12] = 0;
    return 1;
}

static int cpu_family(uint8_t* out_family) {
    uint32_t a, b, c, d;
    if (!cpuid_supported() || cpuid_max_leaf() < 1u) return 0;
    cpuid(1, &a, &b, &c, &d);
    uint32_t base = (a >> 8) & 0x0F;
    uint32_t ext  = (a >> 20) & 0xFF;
    *out_family = (uint8_t)(base + (base == 0x0F ? ext : 0));
    return 1;
}

struct acpi_fadt;
static struct acpi_fadt* acpi_find_fadt(void);

static void cmd_hardware(void) {
    const framebuffer_info_t* f = framebuffer_info();
    net_status_t net;
    net_get_status(&net);
    print("MyOS Hardware Diagnostics\n");
    print("-------------------------\n");
    print("Bootloader: "); print(bootloader_name); putc('\n');
    print("Framebuffer: ");
    if (f && framebuffer_available()) {
        print_uint(f->width); putc('x'); print_uint(f->height); print(" 32bpp");
    } else print("unavailable");
    putc('\n');
    print("Keyboard:    PS/2 ready\n");
    print("Mouse:       "); print(mouse_available() ? "PS/2 ready" : "unavailable"); putc('\n');
    print("ACPI:        "); print(acpi_find_fadt() ? "detected" : "not detected"); putc('\n');
    print("Network:     "); print(net.available ? "RTL8139 ready" : "unavailable"); putc('\n');
    print("Filesystem:  "); print(fs_available() ? "RAMFS ready" : "unavailable"); putc('\n');
    print("Scheduler:   "); print_uint((uint32_t)process_count); print(" tasks / RR ");
    print_uint(SCHED_QUANTUM); print(" tick quantum\n");
}

static void cmd_sysinfo(void) {
    char vendor[13] = {0};
    int have_cpuid = cpu_vendor(vendor, sizeof(vendor));
    uint8_t family = 0;
    int have_family = cpu_family(&family);

    print_color("MyOS System Information\n", 0x0B);
    print("------------------------\n");
    print("OS:         MyOS v0.5\n");
    print("Kernel:     32-bit i386 / Multiboot2\n");
    print("CPU:        ");
    if (have_cpuid) print(vendor); else print("CPUID unavailable");
    putc('\n');
    print("CPU family: ");
    if (have_family) print_uint(family); else print("unavailable");
    putc('\n');
    print("RAM:        ");
    print_uint((mem_lower_kib + mem_upper_kib) / 1024u);
    print(" MiB (reported by Multiboot)\n");
    print("Bootloader: "); print(bootloader_name); putc('\n');
    print("Theme:      "); print(current_theme); putc('\n');
    print("Graphics:   "); print(gui_available() ? "framebuffer" : "VGA text"); putc('\n');
    print("Mouse:      "); print(mouse_available() ? "PS/2 ready" : "not available"); putc('\n');
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

static void cmd_taskmgr(void) {
    print("MyOS Task Manager\n");
    print("-----------------\n");
    print("PID  NAME                 STATE       CPU  MEM  PRIO\n");
    print("----------------------------------------------------\n");
    for (int i = 0; i < process_count; ++i) {
        print_uint(processes[i].pid);
        print("    ");
        print(processes[i].name);
        int pad = 21 - (int)str_len(processes[i].name);
        while (pad-- > 0) putc(' ');
        print(process_state_name(processes[i].state));
        print("    ");
        print_uint(processes[i].cpu_percent);
        print("%   ");
        print_uint(processes[i].memory_kib);
        print("K   ");
        print_uint(processes[i].priority);
        putc('\n');
    }
    print("\nRound-robin quantum: ");
    print_uint(SCHED_QUANTUM);
    print(" ticks\n");
    print("Scheduler runs: ");
    if (scheduler_current >= 0) print_uint(processes[scheduler_current].runs);
    else print("0");
    putc('\n');
}

static void cmd_taskmgr_control(const char* arg) {
    while (*arg == ' ') ++arg;
    if (!*arg) {
        cmd_taskmgr();
        return;
    }
    if (starts_with(arg, "terminate ")) {
        uint32_t pid = 0;
        int found = 0;
        arg += 10;
        while (*arg >= '0' && *arg <= '9') {
            pid = pid * 10u + (uint32_t)(*arg - '0');
            found = 1;
            ++arg;
        }
        if (found && !*arg && scheduler_control(pid, 1)) print("Task terminated.\n");
        else print("Cannot terminate task.\n");
        return;
    }
    if (starts_with(arg, "restart ")) {
        uint32_t pid = 0;
        int found = 0;
        arg += 8;
        while (*arg >= '0' && *arg <= '9') {
            pid = pid * 10u + (uint32_t)(*arg - '0');
            found = 1;
            ++arg;
        }
        if (found && !*arg && scheduler_control(pid, 0)) print("Task restarted.\n");
        else print("Cannot restart task.\n");
        return;
    }
    print("Usage: taskmgr | taskmgr terminate PID | taskmgr restart PID\n");
}
static void cmd_ls(void) {
    fs_entry_t entries[FS_MAX_FILES];
    int count = fs_list(entries, FS_MAX_FILES);
    print("Filesystem entries:\n");
    for (int i = 0; i < count; ++i) {
        print(entries[i].directory ? "[DIR]  " : "[FILE] ");
        print(entries[i].name);
        if (!entries[i].directory) {
            print("  ");
            print_uint(entries[i].size);
            print(" bytes");
        }
        putc('\n');
    }
}

static void cmd_cat(const char* path) {
    char data[FS_DATA_MAX];
    int n = fs_read(path, data, sizeof(data));
    if (n < 0) {
        print("Unable to read file.\n");
        return;
    }
    print(data);
    if (n == 0 || data[n - 1] != '\n') putc('\n');
}

static void cmd_touch(const char* path, int directory) {
    int result = directory ? fs_create_dir(path) : fs_create_file(path);
    if (result < 0) print("Unable to create entry.\n");
    else print(directory ? "Directory created.\n" : "File created.\n");
}

static void cmd_rm(const char* path) {
    if (fs_delete(path)) print("Entry deleted.\n");
    else print("Entry not found.\n");
}

static void cmd_mv(const char* arg) {
    const char* p = arg;
    while (*p == ' ') ++p;
    const char* mid = p;
    while (*mid && *mid != ' ') ++mid;
    if (!*mid) {
        print("Usage: mv OLD NEW\n");
        return;
    }
    char old_path[FS_NAME_MAX];
    int n = 0;
    while (p < mid && n < FS_NAME_MAX - 1) old_path[n++] = *p++;
    old_path[n] = 0;
    while (*mid == ' ') ++mid;
    if (!*mid || fs_rename(old_path, mid) == 0) print("Unable to rename entry.\n");
    else print("Entry renamed.\n");
}

static void cmd_write_file(const char* arg) {
    const char* p = arg;
    while (*p == ' ') ++p;
    const char* mid = p;
    while (*mid && *mid != ' ') ++mid;
    if (!*mid) {
        print("Usage: write PATH TEXT\n");
        return;
    }
    char path[FS_NAME_MAX];
    int n = 0;
    while (p < mid && n < FS_NAME_MAX - 1) path[n++] = *p++;
    path[n] = 0;
    while (*mid == ' ') ++mid;
    if (!*mid || !fs_write(path, mid, (uint32_t)str_len(mid))) {
        print("Unable to write file.\n");
        return;
    }
    print("File written.\n");
}

static void cmd_fsinfo(void) {
    print("Filesystem: RAMFS\n");
    print("Entries:    ");
    print_uint((uint32_t)fs_count());
    print("\nMax entries: ");
    print_uint(FS_MAX_FILES);
    print("\nMax file size: ");
    print_uint(FS_DATA_MAX - 1u);
    print(" bytes\n");
}

static void network_gui_update(void) {
    net_status_t status;
    gui_net_info_t snapshot;
    net_get_status(&status);
    snapshot.available = status.available;
    snapshot.link_up = status.link_up;
    for (int i = 0; i < 6; ++i) snapshot.mac[i] = status.mac[i];
    snapshot.ip = status.ip;
    snapshot.gateway = status.gateway;
    snapshot.tx_packets = status.tx_packets;
    snapshot.rx_packets = status.rx_packets;
    snapshot.ping_success = status.ping_success;
    snapshot.ping_fail = status.ping_fail;
    gui_network_set_status(&snapshot);
}

static void filesystem_gui_update(void) {
    fs_entry_t entries[16];
    gui_file_info_t snapshot[16];
    int count = fs_list(entries, 16);
    for (int i = 0; i < count; ++i) {
        snapshot[i].index = entries[i].index;
        str_copy(snapshot[i].name, entries[i].name, (int)sizeof(snapshot[i].name));
        snapshot[i].directory = entries[i].directory;
        snapshot[i].size = entries[i].size;
    }
    gui_filemgr_set_data(snapshot, count);
}

static void fs_create_default(int directory) {
    static const char* file_names[] = {
        "/home/newfile.txt", "/home/newfile2.txt", "/home/newfile3.txt",
        "/desktop/newfile.txt", "/downloads/newfile.txt"
    };
    static const char* dir_names[] = {
        "/home/newfolder", "/home/newfolder2", "/desktop/newfolder"
    };
    const char** names = directory ? dir_names : file_names;
    int count = directory ? (int)(sizeof(dir_names) / sizeof(dir_names[0]))
                          : (int)(sizeof(file_names) / sizeof(file_names[0]));
    for (int i = 0; i < count; ++i) {
        int result = directory ? fs_create_dir(names[i]) : fs_create_file(names[i]);
        if (result >= 0) {
            if (!directory) fs_write(names[i], "", 0);
            return;
        }
    }
}

static void fs_open_index(int index) {
    fs_entry_t entry;
    if (!fs_stat("/", &entry)) return;
    if (index < 0 || !fs_stat(fs_data(index) ? fs_data(index) : "/", &entry)) {
        fs_entry_t list[FS_MAX_FILES];
        int count = fs_list(list, FS_MAX_FILES);
        for (int i = 0; i < count; ++i) {
            if (list[i].index == index) {
                entry = list[i];
                break;
            }
        }
    }
    if (entry.index != index || entry.directory) return;
    char data[FS_DATA_MAX];
    int n = fs_read(entry.name, data, sizeof(data));
    gui_open_window(0);
    print("\n--- ");
    print(entry.name);
    print(" ---\n");
    if (n >= 0) print(data);
    else print("Unable to read file.");
    putc('\n');
    prompt();
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
    line_origin_cell = (uint16_t)(row * VGA_WIDTH + col);
    line_rendered_len = 0;
    if (gui_console_enabled && gui_available()) gui_terminal_begin_input();
    line_editor_set_cursor();
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
        int64_t next = (int64_t)n * 10 + (*p->s - '0');
        if (next > 2147483647LL) {
            p->error = 1;
            return 0;
        }
        n = (int32_t)next;
        ++p->s;
    }

    if (!found) p->error = 1;
    return n;
}

static int32_t calc_negate(struct calc_parser* p, int32_t value) {
    if (value == (-2147483647 - 1)) {
        p->error = 1;
        return 0;
    }
    return -value;
}

static int32_t calc_add(struct calc_parser* p, int32_t a, int32_t b) {
    int64_t v = (int64_t)a + b;
    if (v < (-2147483647LL - 1) || v > 2147483647LL) {
        p->error = 1;
        return 0;
    }
    return (int32_t)v;
}

static int32_t calc_sub(struct calc_parser* p, int32_t a, int32_t b) {
    int64_t v = (int64_t)a - b;
    if (v < (-2147483647LL - 1) || v > 2147483647LL) {
        p->error = 1;
        return 0;
    }
    return (int32_t)v;
}

static int32_t calc_mul(struct calc_parser* p, int32_t a, int32_t b) {
    int64_t v = (int64_t)a * b;
    if (v < (-2147483647LL - 1) || v > 2147483647LL) {
        p->error = 1;
        return 0;
    }
    return (int32_t)v;
}

static int32_t calc_div(struct calc_parser* p, int32_t a, int32_t b) {
    if (b == 0 || (a == (-2147483647 - 1) && b == -1)) {
        p->error = 1;
        return 0;
    }
    return a / b;
}

static int32_t calc_mod(struct calc_parser* p, int32_t a, int32_t b) {
    if (b == 0 || (a == (-2147483647 - 1) && b == -1)) {
        p->error = 1;
        return 0;
    }
    return a % b;
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

    if (p->error) return 0;
    if (neg) value = calc_negate(p, value);
    if (p->error) return 0;

    calc_spaces(p);
    if (*p->s == '^') {
        ++p->s;
        int32_t exponent = calc_power(p);
        if (p->error || exponent < 0 || exponent > 31) {
            p->error = 1;
            return 0;
        }
        int32_t base = value;
        int32_t result = 1;
        for (int32_t i = 0; i < exponent; ++i) {
            result = calc_mul(p, result, base);
            if (p->error) return 0;
        }
        value = result;
    }

    return value;
}

static int32_t calc_term(struct calc_parser* p) {
    int32_t result = calc_power(p);
    if (p->error) return 0;

    for (;;) {
        calc_spaces(p);
        char op = *p->s;
        if (op != '*' && op != '/' && op != '%') break;
        ++p->s;

        int32_t rhs = calc_power(p);
        if (p->error) return 0;

        if (op == '*') result = calc_mul(p, result, rhs);
        else if (op == '/') result = calc_div(p, result, rhs);
        else result = calc_mod(p, result, rhs);
        if (p->error) return 0;
    }

    return result;
}

static int32_t calc_expr(struct calc_parser* p) {
    int32_t result = calc_term(p);
    if (p->error) return 0;

    for (;;) {
        calc_spaces(p);
        char op = *p->s;
        if (op != '+' && op != '-') break;
        ++p->s;

        int32_t rhs = calc_term(p);
        if (p->error) return 0;

        if (op == '+') result = calc_add(p, result, rhs);
        else result = calc_sub(p, result, rhs);
        if (p->error) return 0;
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

    if (history_count > 0) {
        int last = (history_head + HISTORY_COUNT - 1) % HISTORY_COUNT;
        if (streq(history[last], text)) {
            history_cursor = history_count;
            return;
        }
    }

    str_copy(history[history_head], text, LINE_MAX);
    history_head = (history_head + 1) % HISTORY_COUNT;
    if (history_count < HISTORY_COUNT) ++history_count;
    history_cursor = history_count;
}

static int history_index(int logical_index) {
    int oldest = (history_head + HISTORY_COUNT - history_count) % HISTORY_COUNT;
    return (oldest + logical_index) % HISTORY_COUNT;
}

static const char* command_names[] = {
    "help","version","clear","ascii","echo","calc","theme",
    "history","profile","whoami","hostname","sysinfo","mem",
    "time","clock","date","calendar","taskmgr","hardware","settings","set",
    "notify","notifications","logs","net","ping","uptime",
    "ls","cat","touch","mkdir","rm","mv","write","fsinfo",
    "reboot","shutdown"
};

static void line_editor_ensure_visible(void) {
    if (gui_console_enabled && gui_available()) return;
    /* The prompt plus a maximum-length command can span two rows.
     * Keep the cursor and the whole editable line inside VGA memory. */
    while ((uint32_t)line_origin_cell + (uint32_t)line_len + 1u >= VGA_CELLS) {
        scroll();
        if (line_origin_cell >= VGA_WIDTH)
            line_origin_cell = (uint16_t)(line_origin_cell - VGA_WIDTH);
        else
            line_origin_cell = 0;
    }
}

static void clipboard_copy_selection(int cut) {
    if (selection_anchor < 0 || selection_anchor == cursor_pos) return;
    int a = selection_anchor;
    int b = cursor_pos;
    if (a > b) { int t = a; a = b; b = t; }
    clipboard_len = b - a;
    if (clipboard_len > LINE_MAX - 1) clipboard_len = LINE_MAX - 1;
    for (int i = 0; i < clipboard_len; ++i) clipboard[i] = line[a + i];
    clipboard[clipboard_len] = 0;
    if (cut) {
        for (int i = a; i < line_len - clipboard_len; ++i)
            line[i] = line[i + clipboard_len];
        line_len -= clipboard_len;
        cursor_pos = a;
        selection_anchor = -1;
        redraw_line();
    }
}

static void clipboard_paste(void) {
    if (clipboard_len <= 0) return;
    int room = (LINE_MAX - 1) - line_len;
    int take = clipboard_len < room ? clipboard_len : room;
    if (take <= 0) return;
    for (int i = line_len; i >= cursor_pos; --i)
        line[i + take] = line[i];
    for (int i = 0; i < take; ++i)
        line[cursor_pos + i] = clipboard[i];
    line_len += take;
    cursor_pos += take;
    selection_anchor = -1;
    redraw_line();
}

static void line_editor_set_cursor(void) {
    if (gui_console_enabled && gui_available()) {
        gui_terminal_set_cursor(cursor_pos);
        gui_terminal_set_selection(selection_anchor, cursor_pos);
        return;
    }
    uint32_t pos = (uint32_t)line_origin_cell + (uint32_t)cursor_pos;
    if (pos >= VGA_CELLS) pos = VGA_CELLS - 1;
    row = (int)(pos / VGA_WIDTH);
    col = (int)(pos % VGA_WIDTH);
    vga_cursor_set((uint16_t)pos);
}

static void redraw_line(void) {
    if (gui_console_enabled && gui_available()) {
        gui_terminal_edit(line, line_len, cursor_pos, color);
        return;
    }
    line_editor_ensure_visible();

    int clear_len = line_rendered_len > line_len ? line_rendered_len : line_len;
    for (int i = 0; i < clear_len; ++i) {
        uint32_t cell = (uint32_t)line_origin_cell + (uint32_t)i;
        if (cell < VGA_CELLS)
            vga[cell] = ((uint16_t)color << 8) | ' ';
    }

    for (int i = 0; i < line_len; ++i) {
        uint32_t cell = (uint32_t)line_origin_cell + (uint32_t)i;
        if (cell < VGA_CELLS)
            vga[cell] = ((uint16_t)color << 8) | (uint8_t)line[i];
    }

    line_rendered_len = line_len;
    line_editor_set_cursor();
}

static void set_line(const char* text) {
    str_copy(line, text, LINE_MAX);
    line_len = (int)str_len(line);
    cursor_pos = line_len;
    selection_anchor = -1;
    redraw_line();
}

static void cmd_history(void) {
    if (history_count == 0) {
        print("History is empty.\n");
        return;
    }
    print("Command history:\n");
    for (int i = 0; i < history_count; ++i) {
        print("  ");
        print_uint((uint32_t)(i + 1));
        print("  ");
        print(history[history_index(i)]);
        putc('\n');
    }
}

static int first_token_len(const char* s) {
    int n = 0;
    while (s[n] && s[n] != ' ') ++n;
    return n;
}

static int command_matches_prefix(const char* name, const char* prefix, int n) {
    for (int i = 0; i < n; ++i) {
        if (!name[i] || name[i] != prefix[i]) return 0;
    }
    return 1;
}

static void cmd_complete(void) {
    if (line_len == 0) {
        set_line("help");
        return;
    }

    int n = first_token_len(line);
    if (n <= 0 || n >= LINE_MAX) return;

    int matches = 0;
    const char* match = 0;

    for (size_t i = 0; i < sizeof(command_names) / sizeof(command_names[0]); ++i) {
        if (command_matches_prefix(command_names[i], line, n)) {
            ++matches;
            match = command_names[i];
        }
    }

    if (matches == 1 && n == line_len) {
        set_line(match);
    } else if (matches > 1) {
        putc('\n');
        for (size_t i = 0; i < sizeof(command_names) / sizeof(command_names[0]); ++i) {
            if (command_matches_prefix(command_names[i], line, n)) {
                print(command_names[i]);
                print("  ");
            }
        }
        putc('\n');
        prompt();
        redraw_line();
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

static void print_ipv4_value(uint32_t ip) {
    print_uint((ip >> 24) & 0xFFu);
    putc('.');
    print_uint((ip >> 16) & 0xFFu);
    putc('.');
    print_uint((ip >> 8) & 0xFFu);
    putc('.');
    print_uint(ip & 0xFFu);
}

static void cmd_net(void) {
    net_status_t status;
    uint16_t vendor = 0, device = 0;
    net_get_status(&status);

    print("MyOS Network Status\n");
    print("-------------------\n");
    print("Driver:     ");
    print(status.available ? "RTL8139" : "unavailable");
    putc('\n');
    print("Link:       ");
    print(status.link_up ? "up" : "down");
    putc('\n');
    print("MAC:        ");
    for (int i = 0; i < 6; ++i) {
        if (i) putc(':');
        print_hex8(status.mac[i]);
    }
    putc('\n');
    print("IP:         "); print_ipv4_value(status.ip); putc('\n');
    print("Gateway:    "); print_ipv4_value(status.gateway); putc('\n');
    print("TX packets: "); print_uint(status.tx_packets); putc('\n');
    print("RX packets: "); print_uint(status.rx_packets); putc('\n');
    print("Ping ok:    "); print_uint(status.ping_success); putc('\n');
    print("Ping fail:  "); print_uint(status.ping_fail); putc('\n');

    if (!status.available && pci_find_network(&vendor, &device)) {
        print("PCI NIC:    detected (unsupported driver) 0x");
        print_hex16(vendor);
        putc(':');
        print_hex16(device);
        putc('\n');
    }
}

static void cmd_ping(const char* host) {
    if (!*host) {
        print("Usage: ping IPv4\n");
        return;
    }
    uint32_t ip = 0;
    if (!net_parse_ipv4(host, &ip)) {
        print("Invalid IPv4 address.\n");
        return;
    }
    print("PING ");
    print(host);
    print(": ");
    if (net_ping_ipv4(ip)) print("reply received.");
    else print("request timed out.");
    putc('\n');
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
    } else if (streq(name, "hardware")) {
        print("hardware - show hardware and kernel diagnostics\n");
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

/* ---------- ACPI power management ---------- */

struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oemid[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oemid[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_gas {
    uint8_t address_space;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t address;
} __attribute__((packed));

struct acpi_fadt {
    struct acpi_sdt_header h;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved1;
    uint8_t preferred_power_management_profile;
    uint16_t sci_interrupt;
    uint32_t smi_command;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_control;
    uint32_t pm1a_event_block;
    uint32_t pm1b_event_block;
    uint32_t pm1a_control_block;
    uint32_t pm1b_control_block;
    uint32_t pm2_control_block;
    uint32_t pm_timer_block;
    uint32_t gpe0_block;
    uint32_t gpe1_block;
    uint8_t pm1_event_length;
    uint8_t pm1_control_length;
    uint8_t pm2_control_length;
    uint8_t pm_timer_length;
    uint8_t gpe0_length;
    uint8_t gpe1_length;
    uint8_t gpe1_base;
    uint8_t cstate_control;
    uint16_t worst_c2_latency;
    uint16_t worst_c3_latency;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alarm;
    uint8_t month_alarm;
    uint8_t century;
    uint16_t boot_architecture_flags;
    uint8_t reserved2;
    uint32_t flags;
    struct acpi_gas reset_reg;
    uint8_t reset_value;
} __attribute__((packed));

static uint8_t acpi_checksum(const void* addr, uint32_t length) {
    const uint8_t* p = (const uint8_t*)addr;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; ++i) sum = (uint8_t)(sum + p[i]);
    return sum;
}

static int acpi_sig4(const struct acpi_sdt_header* h, char a, char b, char c, char d) {
    return h->signature[0] == a && h->signature[1] == b &&
           h->signature[2] == c && h->signature[3] == d;
}

static int acpi_sig8(const struct acpi_rsdp* r) {
    static const char sig[] = "RSD PTR ";
    for (int i = 0; i < 8; ++i) if (r->signature[i] != sig[i]) return 0;
    return 1;
}

static struct acpi_rsdp* acpi_find_rsdp(void) {
    uint16_t ebda_segment;
    __asm__ volatile("movw 0x40E, %0" : "=r"(ebda_segment) : : "memory");
    uint32_t ebda = (uint32_t)ebda_segment << 4;
    uint32_t ranges[2][2] = {
        { ebda, ebda ? ebda + 1024u : 0u },
        { 0x000E0000u, 0x00100000u }
    };

    for (int r = 0; r < 2; ++r) {
        uint32_t start = ranges[r][0] & ~15u;
        uint32_t end = ranges[r][1];
        if (r == 0 && (start >= 0x000A0000u || end > 0x000A0000u)) continue;
        if (!end || start >= end) continue;
        for (uint32_t p = start; p + 20u <= end; p += 16u) {
            struct acpi_rsdp* rsdp = (struct acpi_rsdp*)(uintptr_t)p;
            if (!acpi_sig8(rsdp)) continue;
            if (acpi_checksum(rsdp, 20u) != 0) continue;
            if (rsdp->revision >= 2) {
                uint32_t len = rsdp->length;
                if (len < 36u || len > 0x100000u || len > end - p ||
                    acpi_checksum(rsdp, len) != 0) continue;
            }
            return rsdp;
        }
    }
    return (struct acpi_rsdp*)0;
}

static struct acpi_fadt* acpi_find_fadt(void) {
    struct acpi_rsdp* rsdp = acpi_find_rsdp();
    if (!rsdp || !rsdp->rsdt_address) return (struct acpi_fadt*)0;

    struct acpi_sdt_header* rsdt = (struct acpi_sdt_header*)(uintptr_t)rsdp->rsdt_address;
    if (!acpi_sig4(rsdt, 'R','S','D','T') || rsdt->length < 36u ||
        rsdt->length > 0x100000u || acpi_checksum(rsdt, rsdt->length) != 0)
        return (struct acpi_fadt*)0;

    uint32_t bytes = rsdt->length - 36u;
    uint32_t count = bytes / 4u;
    const uint32_t* entries = (const uint32_t*)((const uint8_t*)rsdt + 36u);
    for (uint32_t i = 0; i < count; ++i) {
        if (!entries[i]) continue;
        struct acpi_sdt_header* h = (struct acpi_sdt_header*)(uintptr_t)entries[i];
        if (!acpi_sig4(h, 'F','A','C','P') || h->length < 116u) continue;
        if (acpi_checksum(h, h->length) != 0) continue;
        return (struct acpi_fadt*)h;
    }
    return (struct acpi_fadt*)0;
}

static int aml_pkg_length(const uint8_t* p, uint32_t remaining, uint32_t* length, uint32_t* used) {
    if (!p || remaining == 0) return 0;
    uint8_t lead = p[0];
    uint32_t follow = (uint32_t)(lead >> 6);
    if (follow == 0) {
        *length = lead & 0x3Fu;
        *used = 1;
        return 1;
    }
    if (follow > 3 || remaining < follow + 1u) return 0;
    uint32_t value = lead & 0x0Fu;
    for (uint32_t i = 0; i < follow; ++i)
        value |= (uint32_t)p[i + 1] << (4u + 8u * i);
    *length = value;
    *used = follow + 1u;
    return 1;
}

static int aml_read_integer(const uint8_t* p, uint32_t remaining, uint32_t* value, uint32_t* used) {
    if (!p || remaining < 1u) return 0;
    switch (p[0]) {
        case 0x00: *value = 0; *used = 1; return 1;
        case 0x01: *value = 1; *used = 1; return 1;
        case 0x0A:
            if (remaining < 2u) return 0;
            *value = p[1]; *used = 2; return 1;
        case 0x0B:
            if (remaining < 3u) return 0;
            *value = (uint32_t)p[1] | ((uint32_t)p[2] << 8); *used = 3; return 1;
        case 0x0C:
            if (remaining < 5u) return 0;
            *value = (uint32_t)p[1] | ((uint32_t)p[2] << 8) |
                     ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24);
            *used = 5; return 1;
        case 0x0E:
            if (remaining < 9u) return 0;
            if (p[5] || p[6] || p[7] || p[8]) return 0;
            *value = (uint32_t)p[1] | ((uint32_t)p[2] << 8) |
                     ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24);
            *used = 9; return 1;
        default:
            return 0;
    }
}

static int acpi_find_s5(const struct acpi_fadt* fadt, uint16_t* slp_typa, uint16_t* slp_typb) {
    if (!fadt || !fadt->dsdt) return 0;
    struct acpi_sdt_header* dsdt = (struct acpi_sdt_header*)(uintptr_t)fadt->dsdt;
    if (!acpi_sig4(dsdt, 'D','S','D','T') || dsdt->length < 36u ||
        dsdt->length > 0x100000u || acpi_checksum(dsdt, dsdt->length) != 0)
        return 0;

    const uint8_t* data = (const uint8_t*)dsdt;
    for (uint32_t i = 36u; i + 8u < dsdt->length; ++i) {
        if (data[i] != 0x08 || data[i + 1] != '_' || data[i + 2] != 'S' ||
            data[i + 3] != '5' || data[i + 4] != '_') continue;
        const uint8_t* p = data + i + 5u;
        uint32_t remaining = dsdt->length - (i + 5u);
        if (remaining < 2u || p[0] != 0x12) continue;

        uint32_t pkg_len, pkg_used;
        if (!aml_pkg_length(p + 1u, remaining - 1u, &pkg_len, &pkg_used)) continue;
        if (pkg_len < pkg_used + 1u || pkg_len > remaining - 1u) continue;
        const uint8_t* body = p + 1u + pkg_used;
        uint32_t body_left = pkg_len - pkg_used;
        if (!body_left) continue;
        uint32_t elements = body[0];
        if (elements < 2u) continue;
        uint32_t used_a, used_b, a, b;
        if (!aml_read_integer(body + 1u, body_left - 1u, &a, &used_a)) continue;
        if (body_left < 1u + used_a + 1u) continue;
        if (!aml_read_integer(body + 1u + used_a, body_left - 1u - used_a, &b, &used_b)) continue;
        (void)used_b;
        *slp_typa = (uint16_t)(a & 0x07u);
        *slp_typb = (uint16_t)(b & 0x07u);
        return 1;
    }
    return 0;
}

static int acpi_enable_if_needed(const struct acpi_fadt* fadt) {
    if (!fadt || !fadt->pm1a_control_block || fadt->pm1_control_length < 2u) return 0;
    if (inw((uint16_t)fadt->pm1a_control_block) & 1u) return 1;
    if (!fadt->smi_command || !fadt->acpi_enable || fadt->smi_command > 0xFFFFu) return 0;

    outb((uint16_t)fadt->smi_command, fadt->acpi_enable);
    for (uint32_t i = 0; i < 1000000u; ++i) {
        if (inw((uint16_t)fadt->pm1a_control_block) & 1u) return 1;
    }
    return 0;
}

static int acpi_shutdown(void) {
    struct acpi_fadt* fadt = acpi_find_fadt();
    if (!fadt || !fadt->pm1a_control_block || fadt->pm1_control_length < 2u) return 0;
    if (fadt->pm1a_control_block > 0xFFFFu || fadt->pm1b_control_block > 0xFFFFu) return 0;
    if (!acpi_enable_if_needed(fadt)) return 0;

    uint16_t a, b;
    if (!acpi_find_s5(fadt, &a, &b)) return 0;
    uint16_t value_a = (uint16_t)((a << 10) | 0x2000u);
    uint16_t value_b = (uint16_t)((b << 10) | 0x2000u);
    outw((uint16_t)fadt->pm1a_control_block, value_a);
    if (fadt->pm1b_control_block)
        outw((uint16_t)fadt->pm1b_control_block, value_b);
    return 1;
}

static int acpi_reset(void) {
    struct acpi_fadt* fadt = acpi_find_fadt();
    if (!fadt || fadt->h.length < 129u) return 0;
    if (!(fadt->flags & (1u << 10))) return 0;
    if (!fadt->reset_reg.address || fadt->reset_reg.address > 0xFFFFFFFFu) return 0;

    if (fadt->reset_reg.address_space == 1 && fadt->reset_reg.address <= 0xFFFFu) {
        outb((uint16_t)fadt->reset_reg.address, fadt->reset_value);
        return 1;
    }
    if (fadt->reset_reg.address_space == 0) {
        *(volatile uint8_t*)(uintptr_t)(uint32_t)fadt->reset_reg.address = fadt->reset_value;
        return 1;
    }
    return 0;
}

static int i8042_reset(void) {
    for (uint32_t i = 0; i < 1000000u; ++i) {
        if (!(inb(0x64) & 0x02u)) {
            outb(0x64, 0xFE);
            for (volatile uint32_t d = 0; d < 10000u; ++d) { }
            return 1;
        }
    }
    return 0;
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
        print("32-bit i386 | Multiboot2 | Framebuffer GUI\n");
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
    else if (streq(cmd, "hardware")) cmd_hardware();
    else if (starts_with(cmd, "taskmgr ")) {
        const char* arg;
        command_arg_after(cmd, 8, &arg);
        cmd_taskmgr_control(arg);
    }
    else if (streq(cmd, "ls")) cmd_ls();
    else if (streq(cmd, "fsinfo")) cmd_fsinfo();
    else if (starts_with(cmd, "cat ")) {
        const char* arg; command_arg_after(cmd, 4, &arg); cmd_cat(arg);
    }
    else if (starts_with(cmd, "touch ")) {
        const char* arg; command_arg_after(cmd, 6, &arg); cmd_touch(arg, 0);
    }
    else if (starts_with(cmd, "mkdir ")) {
        const char* arg; command_arg_after(cmd, 6, &arg); cmd_touch(arg, 1);
    }
    else if (starts_with(cmd, "rm ")) {
        const char* arg; command_arg_after(cmd, 3, &arg); cmd_rm(arg);
    }
    else if (starts_with(cmd, "mv ")) {
        const char* arg; command_arg_after(cmd, 3, &arg); cmd_mv(arg);
    }
    else if (starts_with(cmd, "write ")) {
        const char* arg; command_arg_after(cmd, 6, &arg); cmd_write_file(arg);
    }
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
        log_event("Reboot requested");
        notify_add("Reboot requested");
        cpu_cli();
        if (!acpi_reset() && !i8042_reset())
            outb(0xCF9, 0x06);
        for (;;) cpu_halt();
    }
    else if (streq(cmd, "shutdown")) {
        print_color("\n[*] Sending ACPI power-off request...\n", 0x0E);
        notify_add("Shutdown requested");
        log_event("Shutdown requested");
        cpu_cli();

        if (!acpi_shutdown()) {
            print_color("ACPI shutdown unavailable; system halted safely.\n", 0x0C);
        }
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
    scheduler_tick();
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
            else if (code == 0x4B) kbd_push(shift_down ? KEY_SHIFT_LEFT : KEY_LEFT);
            else if (code == 0x4D) kbd_push(shift_down ? KEY_SHIFT_RIGHT : KEY_RIGHT);
            else if (code == 0x47) kbd_push(KEY_HOME);
            else if (code == 0x4F) kbd_push(KEY_END);
            else if (code == 0x49) kbd_push(KEY_PAGEUP);
            else if (code == 0x51) kbd_push(KEY_PAGEDOWN);
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

    if (code == 0x1D) {
        ctrl_down = (uint8_t)!released;
        pic_eoi(1);
        return;
    }

    if (released) {
        pic_eoi(1);
        return;
    }

    if (code == 0x3A) {
        caps_lock = (uint8_t)!caps_lock;
        pic_eoi(1);
        return;
    }

    if (ctrl_down && code == 0x26) {
        kbd_push(KEY_CTRL_L);
        pic_eoi(1);
        return;
    }

    if (code == 0x01) {
        kbd_push(KEY_ESCAPE);
        pic_eoi(1);
        return;
    }

    char c = kbd_translate(code);
    if (c) kbd_push((uint8_t)c);

    pic_eoi(1);
}

void irq12_handler(void) {
    mouse_irq_handler();
    pic_eoi(12);
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
    idt_set_gate(44, (uint32_t)irq12_stub);

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

    outb(PIC1_DATA, 0xF8); /* IRQ0, IRQ1 and cascade IRQ2 */
    outb(PIC2_DATA, 0xEF); /* IRQ12 (mouse) */
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
    if (gui_available()) gui_panic((int)interrupt_number);
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
    vga_cursor_init();
    clear_screen();

    if (magic != 0x36D76289u) {
        print_color("Bad Multiboot2 magic: ", 0x0C);
        print_uint(magic);
        print("\nSystem halted.\n");
        cpu_cli();
        for (;;) cpu_halt();
    }

    parse_multiboot_info((uint32_t)mb_info);
    renderer_init();
    gui_init();
    if (gui_available()) {
        const framebuffer_info_t* fb = framebuffer_info();
        mouse_set_bounds(fb->width, fb->height);
    }
    idt_init();
    pic_remap();
    pit_init(100u);
    if (gui_available()) {
        (void)mouse_init();
        gui_console_enabled = 1;
        gui_terminal_clear();
    }

    draw_banner();
    print("Welcome to ");
    print_color("MyOS", 0x0A);
    print(". Type 'help' for the Help Center.\n");
    print("Try: calc (12+8)*3^2 | theme ice | sysinfo | taskmgr\n\n");

    log_event("MyOS boot complete");
    notify_add("MyOS boot completed");
    scheduler_init();
    fs_init();
    filesystem_gui_update();
    net_init();
    network_gui_update();
    debug_write("MYOS_READY\n");
    if (net_available()) debug_write("MYOS_NET_READY\n");
    else debug_write("MYOS_NET_UNAVAILABLE\n");
    cpu_sti();

    prompt();
    if (gui_available()) {
        struct rtc_time initial_gui_time;
        if (rtc_read(&initial_gui_time)) {
            gui_set_clock(initial_gui_time.hour, initial_gui_time.min, initial_gui_time.sec,
                          initial_gui_time.day, initial_gui_time.month, initial_gui_time.year);
        }
    }
    uint32_t last_gui_clock_sync = 0xFFFFFFFFu;

    for (;;) {
        int event;
        int keyboard_budget = 32;
        while (keyboard_budget-- > 0 && (event = kbd_get_event()) >= 0) {
            if (gui_console_enabled && gui_available() &&
                gui_keyboard_event(event, ctrl_down, shift_down)) {
                continue;
            }

            if (ctrl_down) {
                int ctrl_char = event;
                if (ctrl_char >= 'A' && ctrl_char <= 'Z') ctrl_char += 'a' - 'A';
                if (ctrl_char == 'c') {
                    clipboard_copy_selection(0);
                    continue;
                }
                if (ctrl_char == 'x') {
                    clipboard_copy_selection(1);
                    continue;
                }
                if (ctrl_char == 'v') {
                    clipboard_paste();
                    continue;
                }
            }

            if (event == KEY_UP) {
                if (history_count > 0 && history_cursor > 0) {
                    --history_cursor;
                    set_line(history[history_index(history_cursor)]);
                }
            } else if (event == KEY_DOWN) {
                if (history_cursor < history_count - 1) {
                    ++history_cursor;
                    set_line(history[history_index(history_cursor)]);
                } else {
                    history_cursor = history_count;
                    set_line("");
                }
            } else if (event == KEY_HOME || event == KEY_END || event == KEY_PAGEUP || event == KEY_PAGEDOWN) {
                if (event == KEY_HOME) cursor_pos = 0;
                else if (event == KEY_END) cursor_pos = line_len;
                else if (event == KEY_PAGEUP) {
                    gui_terminal_scroll(-6);
                    continue;
                } else {
                    gui_terminal_scroll(6);
                    continue;
                }
                selection_anchor = -1;
                line_editor_set_cursor();
            } else if (event == KEY_SHIFT_LEFT || event == KEY_SHIFT_RIGHT) {
                if (selection_anchor < 0) selection_anchor = cursor_pos;
                if (event == KEY_SHIFT_LEFT && cursor_pos > 0) --cursor_pos;
                if (event == KEY_SHIFT_RIGHT && cursor_pos < line_len) ++cursor_pos;
                line_editor_set_cursor();
            } else if (event == KEY_LEFT) {
                if (selection_anchor >= 0 && selection_anchor != cursor_pos) {
                    cursor_pos = selection_anchor < cursor_pos ? selection_anchor : cursor_pos;
                    selection_anchor = -1;
                } else if (cursor_pos > 0) {
                    --cursor_pos;
                }
                line_editor_set_cursor();
            } else if (event == KEY_RIGHT) {
                if (selection_anchor >= 0 && selection_anchor != cursor_pos) {
                    cursor_pos = selection_anchor > cursor_pos ? selection_anchor : cursor_pos;
                    selection_anchor = -1;
                } else if (cursor_pos < line_len) {
                    ++cursor_pos;
                }
                line_editor_set_cursor();
            } else if (event == KEY_DELETE || event == '\b') {
                int sel_a = selection_anchor;
                int sel_b = cursor_pos;
                if (sel_a >= 0 && sel_a != sel_b) {
                    if (sel_a > sel_b) { int t = sel_a; sel_a = sel_b; sel_b = t; }
                    for (int i = sel_a; i < line_len - (sel_b - sel_a); ++i)
                        line[i] = line[i + (sel_b - sel_a)];
                    line_len -= sel_b - sel_a;
                    cursor_pos = sel_a;
                    selection_anchor = -1;
                    redraw_line();
                } else if (event == KEY_DELETE && cursor_pos < line_len) {
                    for (int i = cursor_pos; i < line_len - 1; ++i)
                        line[i] = line[i + 1];
                    --line_len;
                    redraw_line();
                } else if (event == '\b' && cursor_pos > 0) {
                    for (int i = cursor_pos - 1; i < line_len - 1; ++i)
                        line[i] = line[i + 1];
                    --line_len;
                    --cursor_pos;
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
                selection_anchor = -1;
                line[0] = 0;
                line_rendered_len = 0;
                prompt();
            } else if (event == KEY_CTRL_L) {
                gui_terminal_clear();
                clear_screen();
                prompt();
            } else if (event == '\t') {
                cmd_complete();
            } else if (ctrl_down && event >= '1' && event <= '5') {
                if (gui_console_enabled && gui_available()) {
                    gui_open_window(event - '1');
                    continue;
                }
            } else if (event >= 32 && event < 127) {
                if (selection_anchor >= 0 && selection_anchor != cursor_pos) {
                    int a = selection_anchor, b = cursor_pos;
                    if (a > b) { int t = a; a = b; b = t; }
                    for (int i = a; i < line_len - (b - a); ++i) line[i] = line[i + (b - a)];
                    line_len -= b - a;
                    cursor_pos = a;
                    selection_anchor = -1;
                }
                if (line_len < LINE_MAX - 1) {
                    for (int i = line_len; i > cursor_pos; --i) line[i] = line[i - 1];
                    line[cursor_pos++] = (char)event;
                    ++line_len;
                    redraw_line();
                }
            }
        }

        if (gui_available()) {
            gui_set_runtime_ticks(timer_ticks);
            mouse_event_t mouse_event;
            int mouse_budget = 12;
            while (mouse_budget-- > 0 && mouse_poll(&mouse_event)) {
                /* Bound mouse work per scheduler pass so a busy PS/2 stream
                 * cannot starve keyboard input or GUI actions. */
                gui_mouse_event(mouse_event.dx, mouse_event.dy, mouse_event.wheel,
                                mouse_event.buttons);
            }

            if (timer_ticks != last_gui_clock_sync && (timer_ticks % 100u) == 0u) {
                last_gui_clock_sync = timer_ticks;
                struct rtc_time gui_time;
                if (rtc_read(&gui_time)) {
                    gui_set_clock(gui_time.hour, gui_time.min, gui_time.sec,
                                  gui_time.day, gui_time.month, gui_time.year);
                }
            }

            int requested_cursor = 0;
            if (gui_take_terminal_cursor(&requested_cursor)) {
                if (requested_cursor < 0) requested_cursor = 0;
                if (requested_cursor > line_len) requested_cursor = line_len;
                cursor_pos = requested_cursor;
                line_editor_set_cursor();
            }

            if ((timer_ticks % 25u) == 0u) {
                scheduler_gui_update();
                filesystem_gui_update();
                network_gui_update();
            }

            gui_action_t action;
            while (gui_poll_action(&action)) {
                if (action.type == GUI_ACTION_REBOOT) execute("reboot");
                else if (action.type == GUI_ACTION_SHUTDOWN) execute("shutdown");
                else if (action.type == GUI_ACTION_TASK_TERMINATE) {
                    if (!scheduler_control((uint32_t)action.arg, 1)) log_event("Task terminate rejected");
                } else if (action.type == GUI_ACTION_TASK_RESTART) {
                    if (!scheduler_control((uint32_t)action.arg, 0)) log_event("Task restart rejected");
                } else if (action.type == GUI_ACTION_FILE_DELETE) {
                    if (fs_delete_index(action.arg)) {
                        log_event("File deleted from GUI");
                        notify_add("File deleted");
                    }
                } else if (action.type == GUI_ACTION_FILE_CREATE) {
                    fs_create_default(0);
                    log_event("File created from GUI");
                } else if (action.type == GUI_ACTION_DIR_CREATE) {
                    fs_create_default(1);
                    log_event("Folder created from GUI");
                } else if (action.type == GUI_ACTION_FILE_OPEN) {
                    fs_open_index(action.arg);
                } else if (action.type == GUI_ACTION_NET_PING) {
                    net_status_t status;
                    net_get_status(&status);
                    if (status.available) {
                        if (net_ping_ipv4(status.gateway)) notify_add("Network ping succeeded");
                        else notify_add("Network ping failed");
                    } else {
                        notify_add("Network driver unavailable");
                    }
                    network_gui_update();
                } else if (action.type == GUI_ACTION_THEME) {
                    static const char* gui_theme_names[] = { "matrix", "ice", "amber", "mono", "light" };
                    int next_theme = 0;
                    for (size_t i = 0; i < sizeof(gui_theme_names) / sizeof(gui_theme_names[0]); ++i) {
                        if (streq(current_theme, gui_theme_names[i])) {
                            next_theme = (int)((i + 1) % (sizeof(gui_theme_names) / sizeof(gui_theme_names[0])));
                            break;
                        }
                    }
                    current_theme = gui_theme_names[next_theme];
                    color = (uint8_t)((themes[next_theme].bg << 4) | (themes[next_theme].fg & 0x0F));
                    gui_set_theme(current_theme);
                    log_event("Theme changed from GUI");
                    notify_add("Theme changed");
                }
            }
            gui_present();
        }

        cpu_halt();
    }
}
