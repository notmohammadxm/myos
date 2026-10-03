#include "../src/graphics/font.h"
#include "../src/net/net.h"
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#define UNIT_TEST 1

/* Pull the kernel's private helpers into this translation unit. */
#include "../src/kernel.c"

/* Link-time stubs for kernel-only symbols; tests exercise pure helpers only. */
#define STUB_ISR(n) void isr##n(void) { }
STUB_ISR(0)  STUB_ISR(1)  STUB_ISR(2)  STUB_ISR(3)
STUB_ISR(4)  STUB_ISR(5)  STUB_ISR(6)  STUB_ISR(7)
STUB_ISR(8)  STUB_ISR(9)  STUB_ISR(10) STUB_ISR(11)
STUB_ISR(12) STUB_ISR(13) STUB_ISR(14) STUB_ISR(15)
STUB_ISR(16) STUB_ISR(17) STUB_ISR(18) STUB_ISR(19)
STUB_ISR(20) STUB_ISR(21) STUB_ISR(22) STUB_ISR(23)
STUB_ISR(24) STUB_ISR(25) STUB_ISR(26) STUB_ISR(27)
STUB_ISR(28) STUB_ISR(29) STUB_ISR(30) STUB_ISR(31)
#undef STUB_ISR
void irq0_stub(void) { }
void irq1_stub(void) { }
void irq12_stub(void) { }
void idt_load(uint32_t address) { (void)address; }

static int failures = 0;

static void check(int condition) {
    if (!condition) ++failures;
}

static int calc_eval(const char* expression, int32_t expected) {
    struct calc_parser p = { expression, 0 };
    int32_t value = calc_expr(&p);
    calc_spaces(&p);
    int ok = !p.error && *p.s == 0 && value == expected;
    check(ok);
    return ok;
}

static void test_calculator(void) {
    check(calc_eval("(12+8)*3^2-5", 175));
    check(calc_eval("2147483647", 2147483647));
    struct calc_parser p0 = { "2147483648", 0 };
    (void)calc_expr(&p0); check(p0.error);
    struct calc_parser p1 = { "2147483647+1", 0 };
    (void)calc_expr(&p1); check(p1.error);
    struct calc_parser p2 = { "(-2147483647-1)/-1", 0 };
    (void)calc_expr(&p2); check(p2.error);
    struct calc_parser p3 = { "2^30", 0 };
    check(calc_expr(&p3) == 1073741824 && !p3.error);
    struct calc_parser p4 = { "2^31", 0 };
    (void)calc_expr(&p4); check(p4.error);
}

static void test_gui_calculator(void) {
    int32_t value = 0;
    check(gui_test_calculate("2+3*4", &value) && value == 14);
    check(gui_test_calculate("(12+8)*3", &value) && value == 60);
    check(gui_test_calculate("20/5+7", &value) && value == 11);
    check(gui_test_calculate("17%5", &value) && value == 2);
    check(gui_test_calculate("-8+3", &value) && value == -5);
    check(!gui_test_calculate("1/0", &value));
    check(!gui_test_calculate("2+", &value));
}

static void test_history_ring(void) {
    history_count = 0;
    history_head = 0;
    history_cursor = 0;
    for (int i = 0; i < 20; ++i) {
        char s[LINE_MAX];
        s[0] = 'c';
        s[1] = (char)('0' + (i / 10));
        s[2] = (char)('0' + (i % 10));
        s[3] = 0;
        history_add(s);
    }
    check(history_count == HISTORY_COUNT);
    for (int i = 0; i < HISTORY_COUNT; ++i) {
        int expected = i + 4;
        const char* s = history[history_index(i)];
        check(s[0] == 'c' && s[1] == (char)('0' + expected / 10) &&
              s[2] == (char)('0' + expected % 10) && s[3] == 0);
    }
}

static void test_completion_prefix(void) {
    check(command_matches_prefix("shutdown", "shut", 4));
    check(command_matches_prefix("shutdown", "shutdown", 8));
    check(!command_matches_prefix("shutdown", "shutdownx", 9));
    check(!command_matches_prefix("set", "settings", 8));
}

static void checksum_table(uint8_t* table, uint32_t length) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < length; ++i) sum += table[i];
    table[9] = (uint8_t)(0u - (uint8_t)sum);
}

static void test_acpi_s5_parser(void) {
    const size_t page = 4096;
    uint8_t* dsdt = (uint8_t*)mmap(NULL, page, PROT_READ | PROT_WRITE,
                                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    check(dsdt != MAP_FAILED && (uintptr_t)dsdt <= 0xFFFFFFFFu);
    if (dsdt == MAP_FAILED || (uintptr_t)dsdt > 0xFFFFFFFFu) return;

    memset(dsdt, 0, page);
    struct acpi_sdt_header* h = (struct acpi_sdt_header*)dsdt;
    h->signature[0] = 'D'; h->signature[1] = 'S';
    h->signature[2] = 'D'; h->signature[3] = 'T';
    h->length = 48;
    uint8_t* aml = dsdt + 36;
    aml[0] = 0x08; aml[1] = '_'; aml[2] = 'S'; aml[3] = '5'; aml[4] = '_';
    aml[5] = 0x12; aml[6] = 0x06; aml[7] = 0x02;
    aml[8] = 0x0A; aml[9] = 0x05; aml[10] = 0x0A; aml[11] = 0x05;
    checksum_table(dsdt, h->length);

    struct acpi_fadt fadt = {0};
    fadt.dsdt = (uint32_t)(uintptr_t)dsdt;
    uint16_t a = 0, b = 0;
    check(acpi_find_s5(&fadt, &a, &b));
    check(a == 5 && b == 5);

    dsdt[20] ^= 1;
    check(!acpi_find_s5(&fadt, &a, &b));
    munmap(dsdt, page);
}


static void test_framebuffer_gui(void) {
    const size_t page = 4096;
    uint8_t* fb = (uint8_t*)mmap(NULL, page * 1024u, PROT_READ | PROT_WRITE,
                                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    check(fb != MAP_FAILED && (uintptr_t)fb <= 0xFFFFFFFFu);
    if (fb == MAP_FAILED || (uintptr_t)fb > 0xFFFFFFFFu) return;

    framebuffer_set_info((uint64_t)(uintptr_t)fb, 1024u * 4u, 1024u, 600u,
                         32u, 1u, 16u, 8u, 8u, 8u, 0u, 8u);
    check(framebuffer_available());
    check(framebuffer_make_color(255, 0, 0) == 0x00FF0000u);
    renderer_init();
    gui_init();
    check(gui_available());
    gui_terminal_begin_input();
    gui_terminal_edit("TEST", 4, 2, 0x0A);
    gui_terminal_set_cursor(1);
    gui_present();
    check(*(uint32_t*)fb != 0u || ((uint32_t*)fb)[1] != 0u);
    gui_mouse_event(4, 3, 0, 1);
    gui_mouse_event(0, 0, 0, 0);
    gui_present();
    framebuffer_set_info(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    munmap(fb, page * 1024u);
}

static void test_filesystem(void) {
    fs_init();
    check(fs_available());
    check(fs_count() >= 5);

    char data[FS_DATA_MAX];
    int n = fs_read("/home/readme.txt", data, sizeof(data));
    check(n == 16 && data[0] == 'w' && data[1] == 'e');

    int index = fs_create_file("/home/unit-test.txt");
    check(index > 0);
    check(fs_write("/home/unit-test.txt", "abc", 3));
    n = fs_read("/home/unit-test.txt", data, sizeof(data));
    check(n == 3 && data[0] == 'a' && data[2] == 'c');
    check(fs_rename("/home/unit-test.txt", "/home/unit-test-renamed.txt"));
    check(fs_stat("/home/unit-test-renamed.txt", 0) == 0);
    check(fs_delete("/home/unit-test-renamed.txt"));
    check(!fs_delete("/"));
    check(fs_create_file("relative.txt") < 0);
}

static void test_scheduler_model(void) {
    scheduler_init();
    check(process_count == PROCESS_MAX);
    check(scheduler_current >= 0 && scheduler_current < process_count);
    check(processes[scheduler_current].state == PROCESS_RUNNING);
    int first = scheduler_current;
    for (int i = 0; i < SCHED_QUANTUM; ++i) {
        ++timer_ticks;
        scheduler_tick();
    }
    check(scheduler_current != first);
}

static void test_mouse_sensitivity(void) {
    mouse_set_sensitivity(200);
    check(mouse_get_sensitivity() == 200);
    mouse_set_sensitivity(1);
    check(mouse_get_sensitivity() == 25);
    mouse_set_sensitivity(255);
    check(mouse_get_sensitivity() == 200);
    mouse_set_sensitivity(100);
    check(mouse_get_sensitivity() == 100);
}

static void test_line_editor(void) {
    for (int i = 0; i < VGA_CELLS; ++i) unit_test_vga[i] = 0x0A20;

    line_origin_cell = 10;
    line_len = 3;
    line_rendered_len = 3;
    cursor_pos = 1;
    line[0] = 'a'; line[1] = 'b'; line[2] = 'c'; line[3] = 0;
    redraw_line();
    check(cursor_pos == 1);
    check(unit_test_vga[10] == (((uint16_t)color << 8) | 'a'));
    check(unit_test_vga[11] == (((uint16_t)color << 8) | 'b'));
    check(unit_test_vga[12] == (((uint16_t)color << 8) | 'c'));

    line_origin_cell = 1990;
    line_len = 15;
    line_rendered_len = 0;
    cursor_pos = 15;
    redraw_line();
    check(line_origin_cell == 1910);
    check(row == 24 && col == 5);
}

static void test_network_helpers(void) {
    uint32_t ip = 0;
    check(net_parse_ipv4("10.0.2.2", &ip));
    check(ip == 0x0A000202u);
    check(net_parse_ipv4("255.255.255.255", &ip));
    check(ip == 0xFFFFFFFFu);
    check(!net_parse_ipv4("256.1.1.1", &ip));
    check(!net_parse_ipv4("10.0.2", &ip));
    check(!net_parse_ipv4("10.0.2.2x", &ip));
}

static void test_font_presence(void) {
    const uint8_t* a = font8x14_get('A');
    const uint8_t* z = font8x14_get('Z');
    int any_a = 0, any_z = 0;
    for (int i = 0; i < FONT8X14_H; ++i) {
        any_a |= a[i] != 0;
        any_z |= z[i] != 0;
    }
    check(any_a && any_z);
}

int test_main(void) {
    test_calculator();
    test_gui_calculator();
    test_history_ring();
    test_completion_prefix();
    test_acpi_s5_parser();
    test_line_editor();
    test_filesystem();
    test_scheduler_model();
    test_mouse_sensitivity();
    test_framebuffer_gui();
    test_network_helpers();
    test_font_presence();
    return failures;
}

int main(void) { return test_main(); }
