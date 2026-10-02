#include "gui.h"
#include "../graphics/framebuffer.h"
#include "../graphics/renderer.h"
#include "../graphics/font.h"
#include <stddef.h>

#define FONT_W FONT8X14_W
#define FONT_H FONT8X14_H
#define TERM_COLS 96
#define TERM_ROWS 48
#define TERM_PAD 14
#define TOPBAR_H 62
#define FOOTER_H 84
#define TITLE_H 42
#define MIN_WINDOW_W 300
#define MIN_WINDOW_H 220
#define WINDOW_COUNT 5
#define DOCK_W 700
#define DOCK_H 64
#define DOCK_CELL_W 112
#define DOCK_Y_FROM_BOTTOM 76
#define LAUNCHER_W 320
#define LAUNCHER_H 292
struct gui_window {
    int x, y, w, h;
    int dragging;
    int drag_dx, drag_dy;
    int visible;
    int minimized;
};

static int ready;
static int dirty;
static int dirty_x0, dirty_y0, dirty_x1, dirty_y1;
static const char* theme_name = "matrix";
static uint32_t bg, panel, panel2, accent, text, muted, border, terminal_bg;
static uint32_t success, danger, warning;
static char term[TERM_ROWS][TERM_COLS];
static uint8_t term_attr[TERM_ROWS][TERM_COLS];
static int tx, ty;
static int input_x, input_y, input_rendered_len;
static int input_active;
static int selection_anchor = -1;
static int selection_cursor = 0;
static int mouse_x_pos;
static int mouse_y_pos;
static uint8_t mouse_prev_buttons;
static int focused_window;
static uint32_t runtime_ticks;
static int clock_hour;
static int clock_minute;
static int clock_second;
static int clock_day;
static int clock_month;
static int clock_year;
static int clock_valid;
static int dock_hot = -1;
static int launcher_open;
static int notification_open = 1;
static int notifications_enabled = 1;
static int animations_enabled = 1;
static int power_open;
static char calc_display[32] = "0";
static int calc_has_value;
static int calc_replace_next;
static int calc_error;
static int term_view_top;
static int term_view_rows;
static int cursor_request_pending;
static int cursor_request_pos;
static int cursor_drawn_x;
static int cursor_drawn_y;
static int cursor_drawn_valid;
static gui_action_t action_queue[4];
static int action_head;
static int action_tail;
static int window_order[WINDOW_COUNT] = { 1, 2, 3, 4, 0 };

static void gui_request_redraw(void);
static void gui_request_terminal_redraw(void);
static struct gui_window windows[WINDOW_COUNT] = {
    { 24, 92, 620, 526, 0, 0, 0, 1, 0 },
    { 666, 92, 334, 238, 0, 0, 0, 1, 0 },
    { 666, 344, 334, 238, 0, 0, 0, 1, 0 },
    { 236, 132, 390, 410, 0, 0, 0, 0, 0 },
    { 552, 152, 390, 410, 0, 0, 0, 0, 0 }
};

static int min_int(int a, int b) { return a < b ? a : b; }
static int max_int(int a, int b) { return a > b ? a : b; }
static size_t gui_strlen(const char* s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void gui_strcopy(char* dst, const char* src, int cap) { int i = 0; if (!dst || cap <= 0) return; while (src && src[i] && i < cap - 1) { dst[i] = src[i]; ++i; } dst[i] = 0; }

static void action_push(int type, const char* value) {
    int next = (action_head + 1) % 4;
    if (next == action_tail) return;
    action_queue[action_head].type = type;
    action_queue[action_head].value = value;
    action_head = next;
}

static void promote_window(int index) {
    int i;
    if (index < 0 || index >= WINDOW_COUNT) return;
    for (i = 0; i < WINDOW_COUNT; ++i)
        if (window_order[i] == index) break;
    if (i >= WINDOW_COUNT) return;
    for (; i < WINDOW_COUNT - 1; ++i) window_order[i] = window_order[i + 1];
    window_order[WINDOW_COUNT - 1] = index;
    focused_window = index;
}

static uint32_t attr_color(uint8_t attr) {
    if (attr == 0x0C) return danger;
    if (attr == 0x0E) return warning;
    if (attr == 0x0B) return accent;
    if (attr == 0x0A) return success;
    return text;
}

static void draw_char(int x, int y, char c, uint32_t color) {
    const uint8_t* glyph = font8x14_get(c);
    for (int r = 0; r < FONT_H; ++r) {
        uint8_t bits = glyph[r];
        for (int col = 0; col < FONT_W; ++col) {
            if (bits & (1u << (FONT_W - 1 - col)))
                renderer_pixel(x + col, y + r, color);
        }
    }
}

static void draw_text(int x, int y, const char* s, uint32_t color) {
    if (!s) return;
    while (*s) {
        if ((unsigned char)*s >= 32u && (unsigned char)*s <= 126u)
            draw_char(x, y, *s, color);
        x += FONT_W;
        ++s;
    }
}

static void set_palette(const char* name) {
    theme_name = name ? name : "matrix";
    if (theme_name[0] == 'i' && theme_name[1] == 'c') {
        bg = framebuffer_make_color(7, 15, 25);
        panel = framebuffer_make_color(11, 23, 38);
        panel2 = framebuffer_make_color(16, 32, 51);
        terminal_bg = framebuffer_make_color(4, 12, 20);
        accent = framebuffer_make_color(102, 214, 255);
        text = framebuffer_make_color(229, 246, 255);
        muted = framebuffer_make_color(133, 169, 190);
        border = framebuffer_make_color(47, 110, 145);
        success = framebuffer_make_color(123, 235, 184);
        warning = framebuffer_make_color(255, 208, 104);
        danger = framebuffer_make_color(255, 102, 128);
    } else if (theme_name[0] == 'a') {
        bg = framebuffer_make_color(23, 16, 7);
        panel = framebuffer_make_color(36, 24, 9);
        panel2 = framebuffer_make_color(49, 32, 12);
        terminal_bg = framebuffer_make_color(13, 8, 3);
        accent = framebuffer_make_color(255, 207, 99);
        text = framebuffer_make_color(255, 242, 206);
        muted = framebuffer_make_color(187, 165, 118);
        border = framebuffer_make_color(135, 96, 30);
        success = framebuffer_make_color(168, 232, 144);
        warning = framebuffer_make_color(255, 207, 99);
        danger = framebuffer_make_color(255, 108, 107);
    } else if (theme_name[0] == 'm' && theme_name[1] == 'o') {
        bg = framebuffer_make_color(12, 12, 12);
        panel = framebuffer_make_color(22, 22, 22);
        panel2 = framebuffer_make_color(29, 29, 29);
        terminal_bg = framebuffer_make_color(7, 7, 7);
        accent = framebuffer_make_color(244, 244, 244);
        text = framebuffer_make_color(241, 241, 241);
        muted = framebuffer_make_color(153, 153, 153);
        border = framebuffer_make_color(82, 82, 82);
        success = framebuffer_make_color(195, 230, 205);
        warning = framebuffer_make_color(235, 210, 170);
        danger = framebuffer_make_color(235, 155, 165);
    } else {
        bg = framebuffer_make_color(4, 12, 8);
        panel = framebuffer_make_color(10, 24, 16);
        panel2 = framebuffer_make_color(14, 31, 21);
        terminal_bg = framebuffer_make_color(2, 8, 4);
        accent = framebuffer_make_color(60, 255, 137);
        text = framebuffer_make_color(218, 255, 231);
        muted = framebuffer_make_color(127, 167, 143);
        border = framebuffer_make_color(37, 104, 61);
        success = framebuffer_make_color(123, 234, 163);
        warning = framebuffer_make_color(255, 205, 95);
        danger = framebuffer_make_color(255, 100, 118);
    }
}

static int inside_window(const struct gui_window* w, int x, int y) {
    return w->visible && !w->minimized &&
           x >= w->x && y >= w->y && x < w->x + w->w && y < w->y + w->h;
}

static void clamp_window(struct gui_window* w) {
    const framebuffer_info_t* f = framebuffer_info();
    int max_x = (int)f->width - w->w;
    int max_y = (int)f->height - FOOTER_H - 4;
    if (max_x < 4) max_x = 4;
    if (max_y < TOPBAR_H + 2) max_y = TOPBAR_H + 2;
    if (w->x < 4) w->x = 4;
    if (w->y < TOPBAR_H + 2) w->y = TOPBAR_H + 2;
    if (w->x > max_x) w->x = max_x;
    if (w->y > max_y) w->y = max_y;
}

static void layout_windows(void) {
    const framebuffer_info_t* f = framebuffer_info();
    int screen_w = (int)f->width;
    int screen_h = (int)f->height;
    int side_w = 334;
    int gap = 14;

    windows[0].x = 24;
    windows[0].y = TOPBAR_H + 30;
    windows[0].w = max_int(MIN_WINDOW_W, screen_w - side_w - 66);
    windows[0].h = max_int(MIN_WINDOW_H, screen_h - TOPBAR_H - FOOTER_H - 42);

    windows[1].x = screen_w - side_w - 24;
    windows[1].y = TOPBAR_H + 30;
    windows[1].w = side_w;
    windows[1].h = 238;

    windows[2].x = screen_w - side_w - 24;
    windows[2].y = windows[1].y + windows[1].h + gap;
    windows[2].w = side_w;
    windows[2].h = 238;

    windows[3].x = 228;
    windows[3].y = TOPBAR_H + 78;
    windows[3].w = 390;
    windows[3].h = 410;

    windows[4].x = max_int(360, screen_w / 2 + 40);
    windows[4].y = TOPBAR_H + 100;
    windows[4].w = 390;
    windows[4].h = 410;

    for (int i = 0; i < WINDOW_COUNT; ++i) clamp_window(&windows[i]);
}


static void draw_icon_terminal(int x, int y, uint32_t c) {
    renderer_rect(x, y, 24, 18, bg);
    renderer_border(x, y, 24, 18, 2, c);
    renderer_line(x + 5, y + 5, x + 9, y + 9, c);
    renderer_line(x + 9, y + 9, x + 5, y + 13, c);
    renderer_line(x + 12, y + 13, x + 19, y + 13, c);
}

static void draw_icon_system(int x, int y, uint32_t c) {
    renderer_rect(x, y, 24, 18, bg);
    renderer_border(x, y, 24, 18, 2, c);
    renderer_rect(x + 4, y + 4, 16, 2, c);
    renderer_rect(x + 4, y + 8, 16, 2, c);
    renderer_rect(x + 4, y + 12, 10, 2, c);
}

static void draw_icon_settings(int x, int y, uint32_t c) {
    renderer_rect(x, y, 24, 18, bg);
    renderer_border(x + 8, y + 1, 8, 16, 1, c);
    renderer_border(x + 1, y + 6, 22, 5, 1, c);
    renderer_rect(x + 11, y + 7, 4, 3, c);
}

static void draw_icon_calculator(int x, int y, uint32_t c) {
    renderer_border(x + 3, y, 18, 18, 2, c);
    renderer_rect(x + 6, y + 3, 12, 3, c);
    renderer_rect(x + 6, y + 9, 3, 3, c);
    renderer_rect(x + 12, y + 9, 3, 3, c);
    renderer_rect(x + 18, y + 9, 3, 3, c);
    renderer_rect(x + 6, y + 14, 3, 3, c);
}

static void draw_icon_monitor(int x, int y, uint32_t c) {
    renderer_border(x + 2, y, 20, 14, 2, c);
    renderer_line(x + 6, y + 11, x + 9, y + 7, c);
    renderer_line(x + 9, y + 7, x + 12, y + 9, c);
    renderer_line(x + 12, y + 9, x + 17, y + 4, c);
    renderer_line(x + 11, y + 14, x + 11, y + 18, c);
    renderer_line(x + 7, y + 18, x + 15, y + 18, c);
}

static void draw_icon_power(int x, int y, uint32_t c) {
    renderer_line(x + 12, y + 1, x + 12, y + 8, c);
    renderer_line(x + 6, y + 6, x + 4, y + 9, c);
    renderer_line(x + 4, y + 9, x + 4, y + 13, c);
    renderer_line(x + 4, y + 13, x + 7, y + 16, c);
    renderer_line(x + 7, y + 16, x + 12, y + 18, c);
    renderer_line(x + 12, y + 18, x + 17, y + 16, c);
    renderer_line(x + 17, y + 16, x + 20, y + 13, c);
    renderer_line(x + 20, y + 13, x + 20, y + 9, c);
    renderer_line(x + 20, y + 9, x + 18, y + 6, c);
}

static void draw_window_controls(const struct gui_window* w) {
    int cy = w->y + 20;
    int min_x = w->x + w->w - 62;
    int close_x = w->x + w->w - 24;
    renderer_rect(min_x - 5, cy - 5, 14, 14, warning);
    renderer_rect(close_x - 5, cy - 5, 14, 14, danger);
    renderer_line(close_x - 2, cy - 2, close_x + 2, cy + 2, bg);
    renderer_line(close_x + 2, cy - 2, close_x - 2, cy + 2, bg);
    renderer_rect(w->x + 15, w->y + 17, 10, 6, accent);
}

static void draw_window(const struct gui_window* w, const char* title, int index) {
    if (!w->visible || w->minimized) return;
    uint32_t line = index == focused_window ? accent : border;
    renderer_rect(w->x + 5, w->y + 6, w->w, w->h, bg);
    renderer_rect(w->x, w->y, w->w, w->h, panel);
    renderer_border(w->x, w->y, w->w, w->h, 1, line);
    renderer_rect(w->x + 1, w->y + 1, w->w - 2, TITLE_H - 1, panel2);
    renderer_rect(w->x + 1, w->y + 1, w->w - 2, 3, line);
    draw_text(w->x + 32, w->y + 14, title, text);
    if (index == focused_window) draw_text(w->x + 32, w->y + 28, "ACTIVE", accent);
    draw_window_controls(w);
}

static void draw_sysinfo(const struct gui_window* w) {
    if (!w->visible || w->minimized) return;
    const framebuffer_info_t* f = framebuffer_info();
    int x = w->x + 18;
    int y = w->y + TITLE_H + 18;
    draw_text(x, y, "SYSTEM", muted);
    draw_text(x + 112, y, "MYOS 0.5", text);
    y += 30;
    draw_text(x, y, "KERNEL", muted);
    draw_text(x + 112, y, "I386 / MULTIBOOT2", text);
    y += 30;
    draw_text(x, y, "DISPLAY", muted);
    if (f) {
        char mode[20];
        int mw=(int)f->width, mh=(int)f->height;
        mode[0]=(char)('0'+(mw/1000)%10); mode[1]=(char)('0'+(mw/100)%10); mode[2]=(char)('0'+(mw/10)%10); mode[3]=(char)('0'+mw%10);
        mode[4]='x'; mode[5]=(char)('0'+(mh/1000)%10); mode[6]=(char)('0'+(mh/100)%10); mode[7]=(char)('0'+(mh/10)%10); mode[8]=(char)('0'+mh%10);
        mode[9]=' '; mode[10]='3'; mode[11]='2'; mode[12]='b'; mode[13]='p'; mode[14]='p'; mode[15]=0;
        draw_text(x + 112, y, mode, success);
    }
    y += 30;
    draw_text(x, y, "INPUT", muted);
    draw_text(x + 112, y, "PS2 MOUSE + KEYBOARD", success);
    y += 30;
    draw_text(x, y, "BOOT", muted);
    draw_text(x + 112, y, "GRAPHICS ONLINE", success);
    y += 30;
    draw_text(x, y, "STATUS", muted);
    draw_text(x + 112, y, "ALL CORE SERVICES READY", success);
    renderer_rect(x, y + 28, w->w - 36, 4, bg);
    renderer_rect(x, y + 28, w->w - 82, 4, success);
    draw_text(x, y + 42, "STABLE UI / PARTIAL PRESENT", accent);
}

static void draw_toggle(int x, int y, int on) {
    renderer_rect(x, y, 58, 22, on ? accent : border);
    renderer_border(x, y, 58, 22, 1, on ? accent : border);
    renderer_rect(x + (on ? 38 : 4), y + 4, 14, 14, on ? bg : muted);
}

static void draw_settings(const struct gui_window* w) {
    if (!w->visible || w->minimized) return;
    int x = w->x + 18;
    int y = w->y + TITLE_H + 20;
    draw_text(x, y, "THEME", muted);
    draw_text(x + 94, y, theme_name, text);
    renderer_border(w->x + w->w - 72, y - 5, 54, 30, 1, accent);
    draw_text(w->x + w->w - 55, y + 3, ">", accent);

    y += 46;
    draw_text(x, y, "NOTIFICATIONS", muted);
    draw_toggle(w->x + w->w - 80, y - 5, notifications_enabled);
    draw_text(w->x + w->w - 142, y + 2, notifications_enabled ? "ON" : "OFF", notifications_enabled ? success : muted);

    y += 42;
    draw_text(x, y, "ANIMATIONS", muted);
    draw_toggle(w->x + w->w - 80, y - 5, animations_enabled);
    draw_text(w->x + w->w - 142, y + 2, animations_enabled ? "ON" : "OFF", animations_enabled ? success : muted);

    y += 42;
    draw_text(x, y, "POINTER", muted);
    draw_text(x + 94, y, "PS2 OPTIMIZED", success);

    y += 38;
    draw_text(x, y, "SESSION", muted);
    draw_text(x + 94, y, "LIVE / NON-PERSISTENT", accent);
}

static void draw_monitor(const struct gui_window* w) {
    if (!w->visible || w->minimized) return;
    const framebuffer_info_t* f = framebuffer_info();
    int x = w->x + 18, y = w->y + TITLE_H + 18;
    uint32_t sec = runtime_ticks / 100u;
    uint32_t mins_total = sec / 60u;
    uint32_t hours = mins_total / 60u;
    uint32_t mins = mins_total % 60u;
    uint32_t secs = sec % 60u;
    char uptime[12];
    uptime[0]=(char)('0'+(hours/10u)%10u); uptime[1]=(char)('0'+hours%10u); uptime[2]=':';
    uptime[3]=(char)('0'+(mins/10u)%10u); uptime[4]=(char)('0'+mins%10u); uptime[5]=':';
    uptime[6]=(char)('0'+(secs/10u)); uptime[7]=(char)('0'+secs%10u); uptime[8]=0;

    draw_text(x, y, "LIVE MONITOR", accent);
    draw_text(x, y + 30, "UPTIME", muted);
    draw_text(x + 94, y + 30, uptime, text);
    draw_text(x, y + 58, "CLOCK", muted);
    if (clock_valid) {
        char t[9];
        t[0]=(char)('0'+clock_hour/10); t[1]=(char)('0'+clock_hour%10); t[2]=':';
        t[3]=(char)('0'+clock_minute/10); t[4]=(char)('0'+clock_minute%10); t[5]=':';
        t[6]=(char)('0'+clock_second/10); t[7]=(char)('0'+clock_second%10); t[8]=0;
        draw_text(x + 94, y + 58, t, success);
    } else draw_text(x + 94, y + 58, "--:--:--", warning);

    draw_text(x, y + 86, "CURSOR", muted);
    char pos[18];
    int mx=mouse_x_pos, my=mouse_y_pos;
    pos[0]=(char)('0'+(mx/1000)%10); pos[1]=(char)('0'+(mx/100)%10); pos[2]=(char)('0'+(mx/10)%10); pos[3]=(char)('0'+mx%10);
    pos[4]=','; pos[5]=' '; pos[6]=(char)('0'+(my/1000)%10); pos[7]=(char)('0'+(my/100)%10); pos[8]=(char)('0'+(my/10)%10); pos[9]=(char)('0'+my%10); pos[10]=0;
    draw_text(x + 94, y + 86, pos, text);

    draw_text(x, y + 114, "RENDER", muted);
    draw_text(x + 94, y + 114, "DIRTY + CLIPPED", success);
    if (f) {
        draw_text(x, y + 142, "MODE", muted);
        draw_text(x + 94, y + 142, "32BPP FRAMEBUFFER", text);
    }
    renderer_rect(x, y + 166, w->w - 36, 1, border);
    draw_text(x, y + 182, "SERVICES", accent);
    const char* services[4] = { "TIMER", "KEYBOARD", "PS2 MOUSE", "GUI LOOP" };
    for (int i = 0; i < 4; ++i) {
        int yy = y + 210 + i * 22;
        renderer_rect(x, yy + 3, 7, 7, success);
        draw_text(x + 16, yy, services[i], text);
        draw_text(x + w->w - 74, yy, "READY", success);
    }
}

static void make_clock_text(char out[9]) {
    if (!out) return;
    if (!clock_valid) {
        gui_strcopy(out, "--:--:--", 9);
        return;
    }
    out[0]=(char)('0'+clock_hour/10); out[1]=(char)('0'+clock_hour%10); out[2]=':';
    out[3]=(char)('0'+clock_minute/10); out[4]=(char)('0'+clock_minute%10); out[5]=':';
    out[6]=(char)('0'+clock_second/10); out[7]=(char)('0'+clock_second%10); out[8]=0;
}

static void draw_desktop_chrome(void) {
    const framebuffer_info_t* f = framebuffer_info();
    int w = (int)f->width;
    int h = (int)f->height;
    char clock[9];
    make_clock_text(clock);

    renderer_rect(0, 0, w, h, bg);
    renderer_rect(0, 0, w, TOPBAR_H, panel2);
    renderer_rect(0, h - FOOTER_H, w, FOOTER_H, panel2);
    renderer_rect(0, TOPBAR_H, 4, h - TOPBAR_H - FOOTER_H, accent);
    renderer_rect(0, TOPBAR_H, w, 1, border);
    renderer_rect(0, h - FOOTER_H, w, 1, border);

    renderer_border(14, 11, 52, 38, 1, launcher_open ? accent : border);
    draw_text(25, 15, "OS", accent);
    draw_text(25, 31, "MENU", muted);
    draw_text(80, 15, "MYOS", accent);
    draw_text(134, 15, "DESKTOP", muted);

    draw_text(w - 270, 14, "CORE ONLINE", success);
    draw_text(w - 134, 14, clock, text);
    if (clock_valid) {
        char date[12];
        date[0]=(char)('0'+(clock_day/10)%10); date[1]=(char)('0'+clock_day%10); date[2]='/';
        date[3]=(char)('0'+(clock_month/10)%10); date[4]=(char)('0'+clock_month%10); date[5]='/';
        date[6]=(char)('0'+(clock_year/1000)%10); date[7]=(char)('0'+(clock_year/100)%10);
        date[8]=(char)('0'+(clock_year/10)%10); date[9]=(char)('0'+clock_year%10); date[10]=0;
        draw_text(w - 134, 34, date, muted);
    }

    /* Notification button. */
    int bx = w - 34, by = 21;
    renderer_border(bx - 10, by - 8, 22, 25, 1, notification_open ? accent : border);
    renderer_rect(bx - 5, by - 1, 11, 10, notification_open ? accent : muted);
    renderer_rect(bx - 2, by + 10, 5, 2, notification_open ? accent : muted);

    /* Dock. Wide cells keep long labels such as SETTINGS inside the tile. */
    int dock_x = (w - DOCK_W) / 2;
    int dock_y = h - DOCK_Y_FROM_BOTTOM;
    renderer_rect(dock_x + 5, dock_y + 5, DOCK_W, DOCK_H, bg);
    renderer_rect(dock_x, dock_y, DOCK_W, DOCK_H, panel);
    renderer_border(dock_x, dock_y, DOCK_W, DOCK_H, 1, border);
    const char* names[6] = { "TERM", "SYSTEM", "SETTINGS", "CALC", "MONITOR", "POWER" };
    for (int i = 0; i < 6; ++i) {
        int cell_x = dock_x + 8 + i * DOCK_CELL_W;
        uint32_t c = dock_hot == i ? accent : muted;
        renderer_rect(cell_x, dock_y + 7, DOCK_CELL_W - 6, DOCK_H - 14, i == dock_hot ? panel2 : bg);
        renderer_border(cell_x, dock_y + 7, DOCK_CELL_W - 6, DOCK_H - 14, 1, c);
        if (i == 0) draw_icon_terminal(cell_x + 9, dock_y + 23, c);
        if (i == 1) draw_icon_system(cell_x + 9, dock_y + 23, c);
        if (i == 2) draw_icon_settings(cell_x + 9, dock_y + 23, c);
        if (i == 3) draw_icon_calculator(cell_x + 9, dock_y + 23, c);
        if (i == 4) draw_icon_monitor(cell_x + 9, dock_y + 23, c);
        if (i == 5) draw_icon_power(cell_x + 9, dock_y + 23, c);
        draw_text(cell_x + 40, dock_y + 25, names[i], c);
    }

    draw_text(20, h - 35, "MYOS CORE", text);
    draw_text(116, h - 35, "GUI READY", success);
    draw_text(w - 142, h - 35, "MOVE WINDOWS", muted);
    draw_text(w - 36, h - 35, "", text);
}

static void draw_launcher(void) {
    if (!launcher_open) return;
    renderer_rect(20, TOPBAR_H + 8, LAUNCHER_W, LAUNCHER_H, bg);
    renderer_rect(14, TOPBAR_H + 2, LAUNCHER_W, LAUNCHER_H, panel);
    renderer_border(14, TOPBAR_H + 2, LAUNCHER_W, LAUNCHER_H, 1, accent);
    draw_text(30, TOPBAR_H + 18, "MYOS APPLICATIONS", accent);
    draw_text(30, TOPBAR_H + 36, "Select a core utility", muted);

    const char* names[5] = { "Terminal", "System Information", "Settings", "Calculator", "System Monitor" };
    const char* desc[5] = { "Shell and command center", "Hardware and display", "Theme and session", "Fast integer math", "Live runtime status" };
    for (int i = 0; i < 5; ++i) {
        int y = TOPBAR_H + 50 + i * 44;
        uint32_t c = (i == dock_hot) ? accent : text;
        renderer_rect(28, y, LAUNCHER_W - 28, 38, i == dock_hot ? panel2 : bg);
        renderer_border(28, y, LAUNCHER_W - 28, 38, 1, border);
        draw_text(42, y + 7, names[i], c);
        draw_text(42, y + 23, desc[i], muted);
    }
}

static void draw_terminal_surface(const struct gui_window* w) {
    if (!w->visible || w->minimized) return;
    int inner_x = w->x + TERM_PAD;
    int inner_y = w->y + TITLE_H + 10;
    int inner_w = w->w - TERM_PAD * 2;
    int inner_h = w->h - TITLE_H - 20;
    term_view_rows = max_int(1, inner_h / FONT_H);
    int cols = max_int(1, inner_w / FONT_W);
    if (cols > TERM_COLS) cols = TERM_COLS;
    if (term_view_top < 0) term_view_top = 0;
    if (term_view_top > TERM_ROWS - term_view_rows) term_view_top = max_int(0, TERM_ROWS - term_view_rows);

    renderer_rect(w->x + 1, w->y + TITLE_H, w->w - 2, w->h - TITLE_H - 1, terminal_bg);
    for (int r = 0; r < term_view_rows; ++r) {
        int source_row = term_view_top + r;
        if (source_row < 0 || source_row >= TERM_ROWS) continue;
        for (int c = 0; c < cols; ++c) {
            char ch = term[source_row][c];
            int selected = 0;
            if (source_row == input_y && selection_anchor >= 0 && selection_anchor != selection_cursor) {
                int a = selection_anchor, b = selection_cursor;
                if (a > b) { int t = a; a = b; b = t; }
                selected = c >= input_x + a && c < input_x + b;
            }
            if (selected) renderer_rect(inner_x + c * FONT_W, inner_y + r * FONT_H, FONT_W, FONT_H, accent);
            if (ch == ' ') continue;
            draw_char(inner_x + c * FONT_W, inner_y + r * FONT_H, ch, selected ? terminal_bg : attr_color(term_attr[source_row][c]));
        }
    }

    if (focused_window == 0 && input_active) {
        int view_y = ty - term_view_top;
        int cx = inner_x + tx * FONT_W;
        int cy = inner_y + view_y * FONT_H;
        if (view_y >= 0 && view_y < term_view_rows)
            renderer_rect(cx, cy + FONT_H - 2, FONT_W - 1, 2, accent);
    }
}

static void terminal_follow_bottom(void) {
    if (term_view_rows <= 0) return;
    if (ty >= term_view_rows) term_view_top = ty - term_view_rows + 1;
    else term_view_top = 0;
}

static void terminal_scroll_buffer(void) {
    for (int r = 1; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c) {
            term[r - 1][c] = term[r][c];
            term_attr[r - 1][c] = term_attr[r][c];
        }
    for (int c = 0; c < TERM_COLS; ++c) {
        term[TERM_ROWS - 1][c] = ' ';
        term_attr[TERM_ROWS - 1][c] = 0x0A;
    }
    ty = TERM_ROWS - 1;
    input_y = max_int(0, input_y - 1);
    term_view_top = max_int(0, term_view_top - 1);
}

static int calc_is_space(char c) { return c == ' ' || c == '\t'; }

static void calc_skip(const char** p) {
    while (**p && calc_is_space(**p)) ++*p;
}

static int32_t calc_add_safe(int32_t a, int32_t b, int* error) {
    int64_t v = (int64_t)a + b;
    if (v < -2147483648LL || v > 2147483647LL) { *error = 1; return 0; }
    return (int32_t)v;
}

static int32_t calc_sub_safe(int32_t a, int32_t b, int* error) {
    int64_t v = (int64_t)a - b;
    if (v < -2147483648LL || v > 2147483647LL) { *error = 1; return 0; }
    return (int32_t)v;
}

static int32_t calc_mul_safe(int32_t a, int32_t b, int* error) {
    int64_t v = (int64_t)a * b;
    if (v < -2147483648LL || v > 2147483647LL) { *error = 1; return 0; }
    return (int32_t)v;
}

static int32_t calc_parse_expr(const char** p, int* error);

static int32_t calc_parse_number(const char** p, int* error) {
    calc_skip(p);
    int neg = 0;
    if (**p == '+' || **p == '-') { neg = **p == '-'; ++*p; calc_skip(p); }
    if (**p < '0' || **p > '9') { *error = 1; return 0; }
    int64_t value = 0;
    while (**p >= '0' && **p <= '9') {
        value = value * 10 + (**p - '0');
        if (value > 2147483648LL) { *error = 1; return 0; }
        ++*p;
    }
    if (neg) value = -value;
    if (value < -2147483648LL || value > 2147483647LL) { *error = 1; return 0; }
    return (int32_t)value;
}

static int32_t calc_parse_factor(const char** p, int* error) {
    calc_skip(p);
    if (**p == '(') {
        ++*p;
        int32_t v = calc_parse_expr(p, error);
        calc_skip(p);
        if (**p != ')') { *error = 1; return 0; }
        ++*p;
        return v;
    }
    return calc_parse_number(p, error);
}

static int32_t calc_parse_term(const char** p, int* error) {
    int32_t result = calc_parse_factor(p, error);
    if (*error) return 0;
    for (;;) {
        calc_skip(p);
        char op = **p;
        if (op != '*' && op != '/' && op != '%') break;
        ++*p;
        int32_t rhs = calc_parse_factor(p, error);
        if (*error) return 0;
        if ((op == '/' || op == '%') && rhs == 0) { *error = 1; return 0; }
        if (op == '*') result = calc_mul_safe(result, rhs, error);
        else if (op == '/') {
            if (result == -2147483648LL && rhs == -1) { *error = 1; return 0; }
            result /= rhs;
        } else {
            if (result == -2147483648LL && rhs == -1) { result = 0; }
            else result %= rhs;
        }
        if (*error) return 0;
    }
    return result;
}

static int32_t calc_parse_expr(const char** p, int* error) {
    int32_t result = calc_parse_term(p, error);
    if (*error) return 0;
    for (;;) {
        calc_skip(p);
        char op = **p;
        if (op != '+' && op != '-') break;
        ++*p;
        int32_t rhs = calc_parse_term(p, error);
        if (*error) return 0;
        result = op == '+' ? calc_add_safe(result, rhs, error) : calc_sub_safe(result, rhs, error);
        if (*error) return 0;
    }
    return result;
}

static void calc_set_result(int32_t value) {
    char buf[32];
    int pos = 0;
    uint32_t v;
    if (value < 0) { buf[pos++] = '-'; v = (uint32_t)(-(int64_t)value); }
    else v = (uint32_t)value;
    char rev[16]; int n = 0;
    if (v == 0) rev[n++] = '0';
    while (v && n < (int)sizeof(rev)) { rev[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    while (n > 0) buf[pos++] = rev[--n];
    buf[pos] = 0;
    gui_strcopy(calc_display, buf, (int)sizeof(calc_display));
    calc_has_value = 1;
    calc_replace_next = 1;
    calc_error = 0;
}

static void calculator_evaluate(void) {
    const char* p = calc_display;
    int error = 0;
    int32_t value = calc_parse_expr(&p, &error);
    calc_skip(&p);
    if (error || *p != 0) {
        gui_strcopy(calc_display, "ERR", (int)sizeof(calc_display));
        calc_error = 1;
        calc_replace_next = 1;
    } else {
        calc_set_result(value);
    }
    gui_request_redraw();
}

static void calculator_backspace(void) {
    size_t n = gui_strlen(calc_display);
    if (calc_replace_next || calc_error || n <= 1) {
        gui_strcopy(calc_display, "0", (int)sizeof(calc_display));
        calc_has_value = 0; calc_replace_next = 0; calc_error = 0;
        return;
    }
    calc_display[n - 1] = 0;
    if (n == 2 && calc_display[0] == '-') gui_strcopy(calc_display, "0", (int)sizeof(calc_display));
}


#ifdef UNIT_TEST
int gui_test_calculate(const char* expression, int32_t* result) {
    if (!expression || !result) return 0;
    const char* p = expression;
    int error = 0;
    int32_t value = calc_parse_expr(&p, &error);
    calc_skip(&p);
    if (error || *p != 0) return 0;
    *result = value;
    return 1;
}
#endif

static void draw_calculator(const struct gui_window* w) {
    if (!w->visible || w->minimized) return;
    int x = w->x + 18, y = w->y + TITLE_H + 16;
    int bw = 78, bh = 48, gap = 8;
    renderer_rect(w->x + 14, y, w->w - 28, 62, terminal_bg);
    renderer_border(w->x + 14, y, w->w - 28, 62, 1, calc_error ? danger : border);
    draw_text(w->x + 24, y + 9, calc_error ? "ERROR" : "READY", calc_error ? danger : success);
    int disp_w = (int)gui_strlen(calc_display) * FONT_W;
    draw_text(w->x + w->w - 28 - disp_w, y + 31, calc_display, text);

    const char* keys[20] = {
        "C","DEL","(",")",
        "7","8","9","/",
        "4","5","6","*",
        "1","2","3","-",
        "0","%","=","+"
    };
    for (int i = 0; i < 20; ++i) {
        int col = i % 4, row = i / 4;
        int bx = x + col * (bw + gap);
        int by = y + 76 + row * (bh + gap);
        uint32_t c = (i == 18) ? accent : ((i == 0 || i == 1) ? danger : ((i == 3 || i == 7 || i == 11 || i == 15 || i == 19) ? warning : text));
        renderer_rect(bx, by, bw, bh, panel2);
        renderer_border(bx, by, bw, bh, 1, c);
        int tw = (int)gui_strlen(keys[i]) * FONT_W;
        draw_text(bx + (bw - tw) / 2, by + 17, keys[i], c);
    }
    draw_text(x, y + 76 + 5 * (bh + gap) - 2, "INTEGER EXPRESSIONS  |  +  -  *  /  %  ( )", muted);
}

static int point_in_calculator_key(int* key_index) {
    if (!key_index) return 0;
    const struct gui_window* w = &windows[3];
    int x = w->x + 18, y = w->y + TITLE_H + 16 + 76;
    int bw = 78, bh = 48, gap = 8;
    int rx = mouse_x_pos - x, ry = mouse_y_pos - y;
    if (!inside_window(w, mouse_x_pos, mouse_y_pos) || rx < 0 || ry < 0) return 0;
    int col = rx / (bw + gap), row = ry / (bh + gap);
    if (col > 3 || row > 4) return 0;
    if ((rx % (bw + gap)) >= bw || (ry % (bh + gap)) >= bh) return 0;
    *key_index = row * 4 + col;
    return 1;
}

static void calculator_append_char(char c) {
    size_t n = gui_strlen(calc_display);
    if (calc_replace_next || calc_error || (n == 1 && calc_display[0] == '0')) {
        n = 0;
        calc_display[0] = 0;
        calc_replace_next = 0;
        calc_error = 0;
    }
    if (n < sizeof(calc_display) - 1) {
        calc_display[n] = c;
        calc_display[n + 1] = 0;
        calc_has_value = 1;
    }
}

static void calculator_key(int index) {
    const char* keys[20] = {
        "C","DEL","(",")","7","8","9","/","4","5","6","*","1","2","3","-","0",".","=","+"
    };
    if (index < 0 || index >= 20) return;
    if (index == 0) {
        gui_strcopy(calc_display, "0", (int)sizeof(calc_display));
        calc_has_value = 0; calc_replace_next = 0; calc_error = 0;
    } else if (index == 1) {
        calculator_backspace();
    } else if (index == 18) {
        calculator_evaluate();
        return;
    } else if (index == 17) {
        calculator_append_char('%');
    } else {
        calculator_append_char(keys[index][0]);
    }
    gui_request_redraw();
}

static void draw_notification(void) {
    if (!notification_open || !notifications_enabled) return;
    const framebuffer_info_t* f = framebuffer_info();
    int w = 300;
    int h = 74;
    int x = (int)f->width - w - 22;
    int y = (int)f->height - FOOTER_H - h - 18;
    renderer_rect(x, y, w, h, panel2);
    renderer_border(x, y, w, h, 1, border);
    renderer_rect(x, y, 4, h, accent);
    draw_text(x + 16, y + 13, "SYSTEM NOTIFICATION", accent);
    draw_text(x + 16, y + 35, "MYOS BOOT COMPLETED", text);
    draw_text(x + 16, y + 53, "GUI SERVICES READY", muted);
}

static void draw_power_menu(void) {
    if (!power_open) return;
    const framebuffer_info_t* f = framebuffer_info();
    int x = (int)f->width - 250;
    int y = (int)f->height - FOOTER_H - 122;
    renderer_rect(x, y, 224, 106, panel2);
    renderer_border(x, y, 224, 106, 1, border);
    draw_text(x + 15, y + 14, "POWER", accent);
    renderer_border(x + 12, y + 35, 92, 48, 1, warning);
    renderer_border(x + 116, y + 35, 92, 48, 1, danger);
    draw_text(x + 27, y + 54, "REBOOT", warning);
    draw_text(x + 127, y + 54, "SHUTDOWN", danger);
}

static void draw_cursor_framebuffer(int x, int y) {
    static const uint8_t shape[17][2] = {
        {0,0},{0,1},{0,2},{0,3},{0,4},{0,5},{0,6},{0,7},{0,8},
        {1,8},{2,10},{3,12},{4,15},{5,15},{5,12},{4,10},{3,8}
    };
    for (unsigned i = 0; i < sizeof(shape)/sizeof(shape[0]); ++i)
        framebuffer_pixel(x + shape[i][0] + 1, y + shape[i][1] + 1, bg);
    for (unsigned i = 0; i < sizeof(shape)/sizeof(shape[0]); ++i)
        framebuffer_pixel(x + shape[i][0], y + shape[i][1], text);
}

static void update_cursor_overlay(void) {
    const int cursor_w = 7;
    const int cursor_h = 17;
    if (cursor_drawn_valid &&
        (cursor_drawn_x != mouse_x_pos || cursor_drawn_y != mouse_y_pos)) {
        renderer_present_rect(cursor_drawn_x, cursor_drawn_y, cursor_w, cursor_h);
    }
    draw_cursor_framebuffer(mouse_x_pos, mouse_y_pos);
    cursor_drawn_x = mouse_x_pos;
    cursor_drawn_y = mouse_y_pos;
    cursor_drawn_valid = 1;
}

static int point_in_launcher_button(void) {
    return mouse_x_pos >= 14 && mouse_x_pos < 66 && mouse_y_pos >= 11 && mouse_y_pos < 49;
}

static int point_in_launcher_item(int* item) {
    if (!launcher_open || !item) return 0;
    int top = TOPBAR_H + 50;
    if (mouse_x_pos < 28 || mouse_x_pos >= 14 + LAUNCHER_W) return 0;
    if (mouse_y_pos < top || mouse_y_pos >= top + 5 * 44) return 0;
    *item = (mouse_y_pos - top) / 44;
    if (*item < 0 || *item >= 5) return 0;
    return 1;
}

static int title_control_at(const struct gui_window* w) {
    int rel_x = mouse_x_pos - w->x;
    if (mouse_y_pos < w->y || mouse_y_pos >= w->y + TITLE_H) return 0;
    if (rel_x >= w->w - 38) return 2;
    if (rel_x >= w->w - 76) return 1;
    return 0;
}

static int point_in_dock(int index) {
    const framebuffer_info_t* f = framebuffer_info();
    int dock_x = ((int)f->width - DOCK_W) / 2;
    int dock_y = (int)f->height - DOCK_Y_FROM_BOTTOM;
    int x = dock_x + 8 + index * DOCK_CELL_W;
    return mouse_x_pos >= x && mouse_x_pos < x + DOCK_CELL_W - 6 &&
           mouse_y_pos >= dock_y + 7 && mouse_y_pos < dock_y + DOCK_H - 7;
}

static int point_in_notification_bell(void) {
    const framebuffer_info_t* f = framebuffer_info();
    int bx = (int)f->width - 34;
    return mouse_x_pos >= bx - 14 && mouse_x_pos < bx + 12 &&
           mouse_y_pos >= 8 && mouse_y_pos < 54;
}

static int point_in_power_button(int which) {
    const framebuffer_info_t* f = framebuffer_info();
    int x = (int)f->width - 250;
    int y = (int)f->height - FOOTER_H - 122;
    int bx = x + 12 + which * 104;
    return power_open && mouse_x_pos >= bx && mouse_x_pos < bx + 92 &&
           mouse_y_pos >= y + 35 && mouse_y_pos < y + 83;
}

static int point_in_settings_row(int row) {
    const struct gui_window* w = &windows[2];
    int y = w->y + TITLE_H + 20;
    int row_h = 34;
    if (row < 0 || row > 2) return 0;
    if (row == 0) return mouse_x_pos >= w->x + 8 && mouse_x_pos < w->x + w->w - 8 &&
                         mouse_y_pos >= y - 8 && mouse_y_pos < y + row_h;
    y += 46;
    if (row == 1) return mouse_x_pos >= w->x + 8 && mouse_x_pos < w->x + w->w - 8 &&
                         mouse_y_pos >= y - 8 && mouse_y_pos < y + row_h;
    y += 42;
    return mouse_x_pos >= w->x + 8 && mouse_x_pos < w->x + w->w - 8 &&
           mouse_y_pos >= y - 8 && mouse_y_pos < y + row_h;
}

static void open_window(int index) {
    if (index < 0 || index >= WINDOW_COUNT) return;
    if (!windows[index].visible || windows[index].minimized) {
        windows[index].minimized = 0;
        windows[index].visible = 1;
    }
    promote_window(index);
    launcher_open = 0;
    power_open = 0;
    gui_request_redraw();
}

void gui_open_window(int index) {
    if (!ready) return;
    open_window(index);
}

static void focus_next_visible(void) {
    for (int i = WINDOW_COUNT - 1; i >= 0; --i) {
        int idx = window_order[i];
        if (windows[idx].visible && !windows[idx].minimized) {
            focused_window = idx;
            return;
        }
    }
    focused_window = -1;
}

void gui_init(void) {
    ready = renderer_available();
    if (!ready) return;
    set_palette("matrix");
    layout_windows();
    for (int r = 0; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c) {
            term[r][c] = ' ';
            term_attr[r][c] = 0x0A;
        }
    tx = ty = 0;
    input_x = input_y = input_rendered_len = 0;
    input_active = 0;
    selection_anchor = -1;
    selection_cursor = 0;
    term_view_top = 0;
    term_view_rows = 1;
    mouse_x_pos = (int)framebuffer_info()->width / 2;
    mouse_y_pos = (int)framebuffer_info()->height / 2;
    mouse_prev_buttons = 0;
    focused_window = 0;
    dock_hot = -1;
    launcher_open = 0;
    notification_open = 1;
    power_open = 0;
    clock_hour = clock_minute = clock_second = 0;
    clock_day = clock_month = 0;
    clock_year = 0;
    clock_valid = 0;
    gui_strcopy(calc_display, "0", (int)sizeof(calc_display));
    calc_has_value = 0;
    calc_replace_next = 0;
    calc_error = 0;
    action_head = action_tail = 0;
    cursor_request_pending = 0;
    cursor_drawn_x = cursor_drawn_y = 0;
    cursor_drawn_valid = 0;
    runtime_ticks = 0;
    gui_request_redraw();
}

int gui_available(void) { return ready != 0; }

void gui_set_theme(const char* name) {
    if (!ready) return;
    set_palette(name);
    gui_request_redraw();
}

void gui_terminal_clear(void) {
    if (!ready) return;
    for (int r = 0; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c) {
            term[r][c] = ' ';
            term_attr[r][c] = 0x0A;
        }
    tx = ty = 0;
    input_x = input_y = input_rendered_len = 0;
    input_active = 0;
    selection_anchor = -1;
    selection_cursor = 0;
    term_view_top = 0;
    gui_request_terminal_redraw();
}

void gui_terminal_begin_input(void) {
    if (!ready) return;
    input_x = tx;
    input_y = ty;
    input_rendered_len = 0;
    input_active = 1;
    selection_anchor = -1;
    selection_cursor = 0;
    terminal_follow_bottom();
    gui_request_terminal_redraw();
}

void gui_terminal_edit(const char* text_in, int len, int cursor_pos, uint8_t shell_color) {
    if (!ready || !input_active || !text_in) return;
    if (len < 0) len = 0;
    if (len >= TERM_COLS) len = TERM_COLS - 1;
    if (cursor_pos < 0) cursor_pos = 0;
    if (cursor_pos > len) cursor_pos = len;

    if (input_y >= 0 && input_y < TERM_ROWS) {
        for (int i = 0; i < input_rendered_len && input_x + i < TERM_COLS; ++i) {
            term[input_y][input_x + i] = ' ';
            term_attr[input_y][input_x + i] = 0x0A;
        }
        for (int i = 0; i < len && input_x + i < TERM_COLS; ++i) {
            term[input_y][input_x + i] = text_in[i];
            term_attr[input_y][input_x + i] = shell_color;
        }
    }
    input_rendered_len = min_int(len, TERM_COLS - input_x);
    if (input_rendered_len < 0) input_rendered_len = 0;
    tx = input_x + cursor_pos;
    ty = input_y;
    selection_cursor = cursor_pos;
    terminal_follow_bottom();
    gui_request_terminal_redraw();
}

void gui_terminal_set_cursor(int cursor_pos) {
    if (!ready || !input_active) return;
    if (cursor_pos < 0) cursor_pos = 0;
    if (cursor_pos > input_rendered_len) cursor_pos = input_rendered_len;
    tx = input_x + cursor_pos;
    ty = input_y;
    selection_cursor = cursor_pos;
    gui_request_terminal_redraw();
}

void gui_terminal_putchar(char c, uint8_t shell_color) {
    if (!ready) return;
    if (c == '\r') {
        tx = 0;
        gui_request_terminal_redraw();
        return;
    }
    if (c == '\b') {
        if (tx > 0) --tx;
        else if (ty > 0) { --ty; tx = TERM_COLS - 1; }
        term[ty][tx] = ' ';
        term_attr[ty][tx] = 0x0A;
        terminal_follow_bottom();
        gui_request_terminal_redraw();
        return;
    }
    if (c == '\t') {
        int n = 4 - (tx % 4);
        while (n--) gui_terminal_putchar(' ', shell_color);
        return;
    }
    if (c == '\n') {
        tx = 0;
        ++ty;
        if (ty >= TERM_ROWS) terminal_scroll_buffer();
        terminal_follow_bottom();
        gui_request_terminal_redraw();
        return;
    }
    if ((unsigned char)c < 32u) return;
    if (tx >= TERM_COLS) {
        tx = 0;
        ++ty;
        if (ty >= TERM_ROWS) terminal_scroll_buffer();
    }
    term[ty][tx] = c;
    term_attr[ty][tx] = shell_color;
    ++tx;
    if (tx >= TERM_COLS) {
        tx = 0;
        ++ty;
        if (ty >= TERM_ROWS) terminal_scroll_buffer();
    }
    terminal_follow_bottom();
    gui_request_terminal_redraw();
}

static void gui_request_redraw_rect(int x, int y, int w, int h) {
    if (!ready || w <= 0 || h <= 0) return;
    const framebuffer_info_t* f = framebuffer_info();
    if (!f) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)f->width) w = (int)f->width - x;
    if (y + h > (int)f->height) h = (int)f->height - y;
    if (w <= 0 || h <= 0) return;
    int x1 = x + w, y1 = y + h;
    if (!dirty) { dirty_x0=x; dirty_y0=y; dirty_x1=x1; dirty_y1=y1; dirty=1; return; }
    if (x < dirty_x0) dirty_x0=x;
    if (y < dirty_y0) dirty_y0=y;
    if (x1 > dirty_x1) dirty_x1=x1;
    if (y1 > dirty_y1) dirty_y1=y1;
}

void gui_request_redraw(void) {
    const framebuffer_info_t* f = framebuffer_info();
    if (!ready || !f) return;
    dirty_x0 = 0; dirty_y0 = 0; dirty_x1 = (int)f->width; dirty_y1 = (int)f->height; dirty = 1;
}

static void gui_request_terminal_redraw(void) {
    const struct gui_window* w = &windows[0];
    gui_request_redraw_rect(w->x, w->y, w->w, w->h);
}

void gui_redraw(void) {
    if (!ready) return;
    dirty = 0;
    draw_desktop_chrome();

    /* Draw bottom-to-top using the current z order. */
    for (int i = 0; i < WINDOW_COUNT; ++i) {
        int idx = window_order[i];
        if (idx == 0) draw_window(&windows[idx], "TERMINAL", idx);
        else if (idx == 1) draw_window(&windows[idx], "SYSTEM INFORMATION", idx);
        else if (idx == 2) draw_window(&windows[idx], "SETTINGS", idx);
        else if (idx == 3) draw_window(&windows[idx], "CALCULATOR", idx);
        else draw_window(&windows[idx], "SYSTEM MONITOR", idx);
    }
    for (int i = 0; i < WINDOW_COUNT; ++i) {
        int idx = window_order[i];
        if (idx == 0) draw_terminal_surface(&windows[idx]);
        else if (idx == 1) draw_sysinfo(&windows[idx]);
        else if (idx == 2) draw_settings(&windows[idx]);
        else if (idx == 3) draw_calculator(&windows[idx]);
        else draw_monitor(&windows[idx]);
    }

    draw_notification();
    draw_power_menu();
    draw_launcher();
}

void gui_present(void) {
    if (!ready) return;
    if (dirty) {
        int px = dirty_x0, py = dirty_y0;
        int pw = dirty_x1 - dirty_x0, ph = dirty_y1 - dirty_y0;
        if (cursor_drawn_valid)
            renderer_present_rect(cursor_drawn_x, cursor_drawn_y, 8, 18);
        renderer_set_clip_rect(px, py, pw, ph);
        gui_redraw();
        renderer_clear_clip();
        renderer_reset_dirty();
        renderer_mark_dirty_rect(px, py, pw, ph);
        renderer_present();
        dirty = 0;
        cursor_drawn_valid = 0;
    }
    update_cursor_overlay();
}

static void handle_title_action(int index, int control) {
    struct gui_window* w = &windows[index];
    int old_x = w->x, old_y = w->y, old_w = w->w, old_h = w->h;
    if (control == 1) {
        w->minimized = 1;
        w->dragging = 0;
        if (focused_window == index) focus_next_visible();
    } else if (control == 2) {
        w->visible = 0;
        w->minimized = 0;
        w->dragging = 0;
        if (focused_window == index) focus_next_visible();
    }
    int x0 = old_x < w->x ? old_x : w->x;
    int y0 = old_y < w->y ? old_y : w->y;
    int x1 = (old_x + old_w) > (w->x + w->w) ? (old_x + old_w) : (w->x + w->w);
    int y1 = (old_y + old_h) > (w->y + w->h) ? (old_y + old_h) : (w->y + w->h);
    gui_request_redraw_rect(x0 - 6, y0 - 6, x1 - x0 + 12, y1 - y0 + 12);
}

void gui_terminal_set_selection(int anchor, int cursor_pos) {
    if (!ready || !input_active) return;
    if (cursor_pos < 0) cursor_pos = 0;
    if (cursor_pos > input_rendered_len) cursor_pos = input_rendered_len;
    if (anchor >= 0) {
        if (anchor > input_rendered_len) anchor = input_rendered_len;
        selection_anchor = anchor;
    } else {
        selection_anchor = -1;
    }
    selection_cursor = cursor_pos;
    gui_request_terminal_redraw();
}

void gui_terminal_scroll(int rows) {
    if (!ready) return;
    int max_top = max_int(0, TERM_ROWS - term_view_rows);
    term_view_top = max_int(0, min_int(term_view_top + rows, max_top));
    gui_request_terminal_redraw();
}

static void terminal_mouse_click(void) {
    const struct gui_window* w = &windows[0];
    int inner_x = w->x + TERM_PAD;
    int inner_y = w->y + TITLE_H + 10;
    int col = (mouse_x_pos - inner_x) / FONT_W;
    int row = (mouse_y_pos - inner_y) / FONT_H + term_view_top;
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    if (row >= TERM_ROWS) row = TERM_ROWS - 1;
    if (row == input_y) {
        int requested = col - input_x;
        if (requested < 0) requested = 0;
        if (requested > input_rendered_len) requested = input_rendered_len;
        cursor_request_pos = requested;
        cursor_request_pending = 1;
    }
    input_active = 1;
    selection_anchor = -1;
    selection_cursor = col - input_x;
    if (selection_cursor < 0) selection_cursor = 0;
    if (selection_cursor > input_rendered_len) selection_cursor = input_rendered_len;
}

void gui_mouse_event(int dx, int dy, int wheel, uint8_t new_buttons) {
    if (!ready) return;
    const framebuffer_info_t* f = framebuffer_info();

    mouse_x_pos += dx;
    mouse_y_pos += dy;
    mouse_x_pos = max_int(0, min_int(mouse_x_pos, (int)f->width - 1));
    mouse_y_pos = max_int(0, min_int(mouse_y_pos, (int)f->height - 1));

    int old_dock_hot = dock_hot;
    dock_hot = -1;
    for (int i = 0; i < 6; ++i) {
        if (point_in_dock(i)) { dock_hot = i; break; }
    }
    if (dock_hot != old_dock_hot)
        gui_request_redraw_rect(((int)f->width - DOCK_W) / 2, (int)f->height - DOCK_Y_FROM_BOTTOM, DOCK_W, DOCK_H);

    if (wheel != 0 && inside_window(&windows[0], mouse_x_pos, mouse_y_pos)) {
        int delta = wheel > 0 ? -3 : 3;
        int max_top = max_int(0, TERM_ROWS - term_view_rows);
        term_view_top = max_int(0, min_int(term_view_top + delta, max_top));
        gui_request_terminal_redraw();
    }

    uint8_t pressed = (uint8_t)((new_buttons ^ mouse_prev_buttons) & new_buttons);
    if (pressed & 1u) {
        if (point_in_launcher_button()) {
            launcher_open = !launcher_open;
            power_open = 0;
            gui_request_redraw_rect(0, 0, LAUNCHER_W + 20, TOPBAR_H + LAUNCHER_H + 12);
        }
        else if (launcher_open) {
            int item = -1;
            if (point_in_launcher_item(&item)) {
                open_window(item);
            } else {
                launcher_open = 0;
                gui_request_redraw_rect(0, 0, LAUNCHER_W + 20, TOPBAR_H + LAUNCHER_H + 12);
            }
        }
        else if (point_in_notification_bell()) {
            notification_open = !notification_open;
            power_open = 0;
            gui_request_redraw();
        }
        else if (point_in_power_button(0)) {
            action_push(GUI_ACTION_REBOOT, 0);
            power_open = 0;
            gui_request_redraw();
        }
        else if (point_in_power_button(1)) {
            action_push(GUI_ACTION_SHUTDOWN, 0);
            power_open = 0;
            gui_request_redraw();
        }
        else if (dock_hot >= 0) {
            if (dock_hot == 5) {
                power_open = !power_open;
                launcher_open = 0;
                gui_request_redraw();
            } else {
                open_window(dock_hot);
            }
        }
        else {
            int hit = -1;
            for (int oi = WINDOW_COUNT - 1; oi >= 0; --oi) {
                int idx = window_order[oi];
                if (inside_window(&windows[idx], mouse_x_pos, mouse_y_pos)) { hit = idx; break; }
            }
            if (hit >= 0) {
                promote_window(hit);
                struct gui_window* w = &windows[hit];
                int control = title_control_at(w);
                if (control) {
                    handle_title_action(hit, control);
                } else if (hit == 0) {
                    terminal_mouse_click();
                } else if (hit == 3) {
                    int key_index = -1;
                    if (point_in_calculator_key(&key_index)) calculator_key(key_index);
                } else if (hit == 2) {
                    if (point_in_settings_row(0)) action_push(GUI_ACTION_THEME, 0);
                    else if (point_in_settings_row(1)) {
                        notifications_enabled = !notifications_enabled;
                        gui_request_redraw_rect(w->x, w->y, w->w, w->h);
                    } else if (point_in_settings_row(2)) {
                        animations_enabled = !animations_enabled;
                        gui_request_redraw_rect(w->x, w->y, w->w, w->h);
                    }
                }
                if (mouse_y_pos < w->y + TITLE_H && !control) {
                    w->dragging = 1;
                    w->drag_dx = mouse_x_pos - w->x;
                    w->drag_dy = mouse_y_pos - w->y;
                }
                gui_request_redraw_rect(w->x - 6, w->y - 6, w->w + 12, w->h + 12);
            } else {
                focused_window = -1;
                power_open = 0;
                gui_request_redraw();
            }
        }
    }

    if (!(new_buttons & 1u)) {
        for (int i = 0; i < WINDOW_COUNT; ++i) windows[i].dragging = 0;
    } else if (focused_window >= 0 && windows[focused_window].dragging) {
        struct gui_window* w = &windows[focused_window];
        int old_x = w->x, old_y = w->y;
        w->x = mouse_x_pos - w->drag_dx;
        w->y = mouse_y_pos - w->drag_dy;
        clamp_window(w);
        int x0 = old_x < w->x ? old_x : w->x;
        int y0 = old_y < w->y ? old_y : w->y;
        int x1 = (old_x + w->w) > (w->x + w->w) ? (old_x + w->w) : (w->x + w->w);
        int y1 = (old_y + w->h) > (w->y + w->h) ? (old_y + w->h) : (w->y + w->h);
        gui_request_redraw_rect(x0 - 6, y0 - 6, x1 - x0 + 12, y1 - y0 + 12);
    }

    mouse_prev_buttons = new_buttons;
}

void gui_set_runtime_ticks(uint32_t ticks) {
    if (!ready) return;
    if (ticks == runtime_ticks) return;
    runtime_ticks = ticks;
    if ((ticks % 25u) == 0u && windows[4].visible && !windows[4].minimized)
        gui_request_redraw_rect(windows[4].x, windows[4].y, windows[4].w, windows[4].h);
    if ((ticks % 100u) == 0u) {
        /* Header clock is pushed by the kernel through gui_set_clock(). */
        gui_request_redraw_rect(0, 0, (int)framebuffer_info()->width, TOPBAR_H);
    }
}

void gui_set_clock(int hour, int minute, int second, int day, int month, int year) {
    if (!ready) return;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        clock_valid = 0;
        gui_request_redraw_rect(0, 0, (int)framebuffer_info()->width, TOPBAR_H);
        return;
    }
    clock_hour = hour;
    clock_minute = minute;
    clock_second = second;
    clock_day = day;
    clock_month = month;
    clock_year = year;
    clock_valid = 1;
    gui_request_redraw_rect(0, 0, (int)framebuffer_info()->width, TOPBAR_H);
}

int gui_take_terminal_cursor(int* cursor_pos) {
    if (!cursor_request_pending || !cursor_pos) return 0;
    *cursor_pos = cursor_request_pos;
    cursor_request_pending = 0;
    return 1;
}

int gui_poll_action(gui_action_t* out) {
    if (!out || action_tail == action_head) return 0;
    *out = action_queue[action_tail];
    action_tail = (action_tail + 1) % 4;
    return 1;
}
