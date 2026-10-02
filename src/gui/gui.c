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
#define TOPBAR_H 54
#define FOOTER_H 72
#define TITLE_H 38
#define MIN_WINDOW_W 300
#define MIN_WINDOW_H 220
#define WINDOW_COUNT 5
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
static int dock_hot = -1;
static int notification_open = 1;
static int notifications_enabled = 1;
static int animations_enabled = 1;
static int power_open;
static char calc_display[32] = "0";
static int calc_has_value;
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
    { 28, 104, 650, 500, 0, 0, 0, 1, 0 },
    { 704, 94, 290, 254, 0, 0, 0, 1, 0 },
    { 704, 372, 290, 246, 0, 0, 0, 1, 0 },
    { 330, 170, 360, 390, 0, 0, 0, 0, 0 },
    { 250, 150, 500, 420, 0, 0, 0, 0, 0 }
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
    int side_w = 306;
    int gap = 24;

    windows[0].x = 28;
    windows[0].y = 104;
    windows[0].w = max_int(MIN_WINDOW_W, screen_w - side_w - 82);
    windows[0].h = max_int(MIN_WINDOW_H, screen_h - TOPBAR_H - FOOTER_H - 48);

    windows[1].x = screen_w - side_w - 24;
    windows[1].y = 94;
    windows[1].w = side_w;
    windows[1].h = 246;

    windows[2].x = screen_w - side_w - 24;
    windows[2].y = windows[1].y + windows[1].h + gap;
    windows[2].w = side_w;
    windows[2].h = max_int(220, screen_h - windows[2].y - FOOTER_H - 12);

    windows[3].x = (screen_w - 360) / 2;
    windows[3].y = TOPBAR_H + 78;
    windows[3].w = 360;
    windows[3].h = 390;

    for (int i = 0; i < WINDOW_COUNT; ++i) clamp_window(&windows[i]);
}

static void draw_icon_terminal(int x, int y, uint32_t c) {
    renderer_border(x, y, 20, 16, 2, c);
    renderer_line(x + 5, y + 5, x + 9, y + 8, c);
    renderer_line(x + 9, y + 8, x + 5, y + 11, c);
    renderer_line(x + 11, y + 11, x + 16, y + 11, c);
}

static void draw_icon_system(int x, int y, uint32_t c) {
    renderer_border(x, y, 20, 16, 2, c);
    renderer_line(x + 4, y + 5, x + 16, y + 5, c);
    renderer_line(x + 4, y + 8, x + 16, y + 8, c);
    renderer_line(x + 4, y + 11, x + 16, y + 11, c);
}

static void draw_icon_settings(int x, int y, uint32_t c) {
    renderer_border(x + 7, y + 1, 6, 14, 2, c);
    renderer_border(x + 1, y + 6, 18, 4, 2, c);
}

static void draw_icon_power(int x, int y, uint32_t c) {
    renderer_line(x + 10, y + 1, x + 10, y + 8, c);
    renderer_rect(x + 5, y + 8, 10, 2, c);
    renderer_line(x + 5, y + 9, x + 4, y + 11, c);
    renderer_line(x + 4, y + 11, x + 6, y + 14, c);
    renderer_line(x + 6, y + 14, x + 10, y + 15, c);
    renderer_line(x + 10, y + 15, x + 14, y + 14, c);
    renderer_line(x + 14, y + 14, x + 16, y + 11, c);
    renderer_line(x + 16, y + 11, x + 15, y + 9, c);
}

static void draw_window_controls(const struct gui_window* w) {
    int y = w->y + 14;
    int min_x = w->x + w->w - 61;
    int max_x = w->x + w->w - 38;
    int close_x = w->x + w->w - 20;
    renderer_line(min_x, y + 5, min_x + 9, y + 5, muted);
    renderer_border(max_x, y + 1, 9, 9, 1, muted);
    renderer_line(close_x - 3, y + 1, close_x + 5, y + 9, danger);
    renderer_line(close_x + 5, y + 1, close_x - 3, y + 9, danger);
}

static void draw_window(const struct gui_window* w, const char* title, int index) {
    if (!w->visible || w->minimized) return;
    uint32_t line = index == focused_window ? accent : border;
    renderer_rect(w->x, w->y, w->w, w->h, panel);
    renderer_border(w->x, w->y, w->w, w->h, 1, line);
    renderer_rect(w->x + 1, w->y + 1, w->w - 2, TITLE_H - 1, panel2);
    renderer_rect(w->x + 12, w->y + 13, 8, 8, index == focused_window ? accent : muted);
    draw_text(w->x + 28, w->y + 12, title, text);
    draw_window_controls(w);
}

static void draw_sysinfo(const struct gui_window* w) {
    if (!w->visible || w->minimized) return;
    int y = w->y + TITLE_H + 22;
    draw_text(w->x + 16, y, "SYSTEM", muted);
    draw_text(w->x + 16, y + 22, "MYOS V0.5", text);
    draw_text(w->x + 158, y, "KERNEL", muted);
    draw_text(w->x + 158, y + 22, "I386", text);

    y += 62;
    draw_text(w->x + 16, y, "CPU", muted);
    renderer_rect(w->x + 16, y + 18, w->w - 32, 8, bg);
    renderer_rect(w->x + 16, y + 18, (w->w - 32) / 9, 8, accent);
    draw_text(w->x + 16, y + 34, "7% ACTIVE", success);

    y += 66;
    draw_text(w->x + 16, y, "MEMORY", muted);
    renderer_rect(w->x + 16, y + 18, w->w - 32, 8, bg);
    renderer_rect(w->x + 16, y + 18, (w->w - 32) / 3, 8, accent);
    draw_text(w->x + 16, y + 34, "28% USED", text);

    y += 66;
    draw_text(w->x + 16, y, "HARDWARE", muted);
    draw_text(w->x + 16, y + 22, "FRAMEBUFFER", success);
    draw_text(w->x + 158, y + 22, "PS2 MOUSE", success);
}

static void draw_toggle(int x, int y, int on) {
    renderer_rect(x, y, 48, 20, on ? accent : border);
    renderer_rect(x + (on ? 28 : 4), y + 4, 12, 12, on ? bg : muted);
}

static void draw_settings(const struct gui_window* w) {
    if (!w->visible || w->minimized) return;
    int y = w->y + TITLE_H + 20;
    draw_text(w->x + 16, y, "THEME", muted);
    draw_text(w->x + 16, y + 22, theme_name, text);
    renderer_border(w->x + w->w - 62, y - 3, 46, 30, 1, border);
    draw_text(w->x + w->w - 47, y + 6, ">", accent);

    y += 66;
    draw_text(w->x + 16, y, "NOTIFICATIONS", muted);
    draw_toggle(w->x + w->w - 64, y - 3, notifications_enabled);

    y += 48;
    draw_text(w->x + 16, y, "ANIMATIONS", muted);
    draw_toggle(w->x + w->w - 64, y - 3, animations_enabled);

    y += 48;
    draw_text(w->x + 16, y, "SESSION", muted);
    draw_text(w->x + 16, y + 22, "NOT PERSISTED", accent);
}

static void draw_monitor(const struct gui_window* w) {
    if (!w->visible || w->minimized) return;
    const framebuffer_info_t* f = framebuffer_info();
    int x = w->x + 18, y = w->y + TITLE_H + 18;
    uint32_t sec = runtime_ticks / 100u;
    int min = (int)(sec / 60u);
    int hour = min / 60;
    min %= 60;
    int s = (int)(sec % 60u);

    draw_text(x, y, "SYSTEM MONITOR", accent);
    draw_text(x, y + 28, "UPTIME", muted);
    draw_text(x + 88, y + 28, "00:00:00", text);
    /* Render HH:MM:SS without sprintf/libc. */
    char clock[9];
    clock[0] = (char)('0' + (hour / 10) % 10); clock[1] = (char)('0' + hour % 10);
    clock[2] = ':'; clock[3] = (char)('0' + min / 10); clock[4] = (char)('0' + min % 10);
    clock[5] = ':'; clock[6] = (char)('0' + s / 10); clock[7] = (char)('0' + s % 10); clock[8] = 0;
    draw_text(x + 88, y + 28, clock, text);

    draw_text(x, y + 62, "FRAMEBUFFER", muted);
    if (f) {
        char mode[24]; int mw = (int)f->width, mh = (int)f->height;
        mode[0]=(char)('0'+(mw/1000)%10); mode[1]=(char)('0'+(mw/100)%10); mode[2]=(char)('0'+(mw/10)%10); mode[3]=(char)('0'+mw%10);
        mode[4]='x'; mode[5]=(char)('0'+(mh/1000)%10); mode[6]=(char)('0'+(mh/100)%10); mode[7]=(char)('0'+(mh/10)%10); mode[8]=(char)('0'+mh%10);
        mode[9]=' '; mode[10]='3'; mode[11]='2'; mode[12]='b'; mode[13]='p'; mode[14]='p'; mode[15]=0;
        draw_text(x + 88, y + 62, mode, success);
    }

    draw_text(x, y + 96, "MOUSE", muted);
    char pos[32];
    pos[0]='X'; pos[1]=':'; pos[2]=' ';
    int mx=mouse_x_pos, my=mouse_y_pos;
    pos[3]=(char)('0'+(mx/1000)%10); pos[4]=(char)('0'+(mx/100)%10); pos[5]=(char)('0'+(mx/10)%10); pos[6]=(char)('0'+mx%10);
    pos[7]=' '; pos[8]='Y'; pos[9]=':'; pos[10]=' ';
    pos[11]=(char)('0'+(my/1000)%10); pos[12]=(char)('0'+(my/100)%10); pos[13]=(char)('0'+(my/10)%10); pos[14]=(char)('0'+my%10); pos[15]=0;
    draw_text(x + 88, y + 96, pos, text);

    draw_text(x, y + 130, "INPUT", muted);
    draw_text(x + 88, y + 130, mouse_prev_buttons ? "MOUSE ACTIVE" : "IDLE", mouse_prev_buttons ? success : text);

    renderer_rect(x, y + 166, w->w - 36, 1, border);
    draw_text(x, y + 186, "KERNEL SERVICES", accent);
    const char* services[6] = { "IRQ0 TIMER", "IRQ1 KEYBOARD", "IRQ12 MOUSE", "FRAMEBUFFER", "GUI EVENT LOOP", "SHELL BACKEND" };
    for (int i = 0; i < 6; ++i) {
        int yy = y + 214 + i * 25;
        renderer_rect(x, yy + 3, 8, 8, success);
        draw_text(x + 18, yy, services[i], text);
        draw_text(x + w->w - 92, yy, "READY", success);
    }
}

static void draw_desktop_chrome(void) {
    const framebuffer_info_t* f = framebuffer_info();
    int w = (int)f->width;
    int h = (int)f->height;

    renderer_rect(0, 0, w, h, bg);
    renderer_rect(0, 0, w, TOPBAR_H, panel2);
    renderer_rect(0, h - FOOTER_H, w, FOOTER_H, panel2);
    renderer_rect(0, TOPBAR_H, 5, h - TOPBAR_H - FOOTER_H, accent);

    draw_text(22, 16, "MYOS", accent);
    draw_text(76, 16, "DESKTOP", muted);
    draw_text(w - 176, 16, "CORE ONLINE", success);
    draw_text(w - 90, 16, "10:42", text);

    /* Notification bell target. */
    int bx = w - 32;
    int by = 13;
    renderer_border(bx - 12, by - 2, 19, 22, 1, notification_open ? accent : border);
    renderer_rect(bx - 8, by + 4, 11, 8, notification_open ? accent : muted);
    renderer_rect(bx - 5, by + 13, 5, 2, notification_open ? accent : muted);

    /* Dock. */
    int dock_w = 584;
    int dock_h = 54;
    int dock_x = (w - dock_w) / 2;
    int dock_y = h - 62;
    renderer_rect(dock_x, dock_y, dock_w, dock_h, panel);
    renderer_border(dock_x, dock_y, dock_w, dock_h, 1, border);

    const char* names[6] = { "TERM", "SYSTEM", "SETTINGS", "CALC", "MONITOR", "POWER" };
    for (int i = 0; i < 6; ++i) {
        int cell_x = dock_x + 7 + i * 96;
        uint32_t c = dock_hot == i ? accent : muted;
        renderer_rect(cell_x, dock_y + 6, 88, 42, i == dock_hot ? panel2 : bg);
        renderer_border(cell_x, dock_y + 6, 88, 42, 1, c);
        if (i == 0) draw_icon_terminal(cell_x + 8, dock_y + 17, c);
        if (i == 1) draw_icon_system(cell_x + 8, dock_y + 17, c);
        if (i == 2) draw_icon_settings(cell_x + 8, dock_y + 17, c);
        if (i == 3) { draw_icon_settings(cell_x + 8, dock_y + 17, c); }
        if (i == 4) draw_icon_system(cell_x + 8, dock_y + 17, c);
        if (i == 5) draw_icon_power(cell_x + 8, dock_y + 17, c);
        draw_text(cell_x + 34, dock_y + 20, names[i], c);
    }

    draw_text(20, h - 38, "MYOS CORE", text);
    draw_text(w - 116, h - 38, "SESSION", muted);
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

static void draw_calculator(const struct gui_window* w) {
    if (!w->visible || w->minimized) return;
    int x = w->x + 18, y = w->y + TITLE_H + 18;
    int bw = 70, bh = 48, gap = 8;
    renderer_rect(w->x + 14, y, w->w - 28, 54, terminal_bg);
    renderer_border(w->x + 14, y, w->w - 28, 54, 1, border);
    draw_text(w->x + w->w - 28 - (int)gui_strlen(calc_display) * FONT_W, y + 18, calc_display, text);
    const char* keys[16] = { "7","8","9","/","4","5","6","*","1","2","3","-","0",".","=","+" };
    for (int i = 0; i < 16; ++i) {
        int col = i % 4, row = i / 4;
        int bx = x + col * (bw + gap);
        int by = y + 68 + row * (bh + gap);
        renderer_rect(bx, by, bw, bh, panel2);
        renderer_border(bx, by, bw, bh, 1, i == 14 ? accent : border);
        draw_text(bx + 28, by + 17, keys[i], i == 14 ? accent : text);
    }
    renderer_rect(x, y + 68 + 4 * (bh + gap), 4 * bw + 3 * gap, 34, panel2);
    renderer_border(x, y + 68 + 4 * (bh + gap), 4 * bw + 3 * gap, 34, 1, danger);
    draw_text(x + 150, y + 68 + 4 * (bh + gap) + 10, "CLEAR", danger);
}

static int point_in_calculator_key(int* key_index) {
    if (!key_index) return 0;
    const struct gui_window* w = &windows[3];
    if (!inside_window(w, mouse_x_pos, mouse_y_pos)) return 0;
    int x = w->x + 18, y = w->y + TITLE_H + 18 + 68;
    int bw = 70, bh = 48, gap = 8;
    int rx = mouse_x_pos - x, ry = mouse_y_pos - y;
    if (rx < 0 || ry < 0) return 0;
    int col = rx / (bw + gap), row = ry / (bh + gap);
    if (col > 3 || row > 3) return 0;
    if ((rx % (bw + gap)) >= bw || (ry % (bh + gap)) >= bh) return 0;
    *key_index = row * 4 + col;
    return 1;
}

static void calculator_key(int index) {
    const char* keys[16] = { "7","8","9","/","4","5","6","*","1","2","3","-","0",".","=","+" };
    if (index < 0 || index >= 16) return;
    if (index == 14) {
        gui_strcopy(calc_display, "0", (int)sizeof(calc_display));
        calc_has_value = 0;
    } else if (index == 13) {
        if (!calc_has_value && gui_strlen(calc_display) < (size_t)sizeof(calc_display)-1) {
            size_t n = gui_strlen(calc_display); calc_display[n] = '.'; calc_display[n+1] = 0; calc_has_value = 1;
        }
    } else {
        size_t n = gui_strlen(calc_display);
        if (n == 1 && calc_display[0] == '0') n = 0;
        if (n < (size_t)sizeof(calc_display)-1) { calc_display[n] = keys[index][0]; calc_display[n+1] = 0; }
        calc_has_value = 1;
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

static int title_control_at(const struct gui_window* w) {
    int rel_x = mouse_x_pos - w->x;
    if (mouse_y_pos < w->y || mouse_y_pos >= w->y + TITLE_H) return 0;
    if (rel_x >= w->w - 32) return 2;
    if (rel_x >= w->w - 68) return 1;
    return 0;
}

static int point_in_dock(int index) {
    const framebuffer_info_t* f = framebuffer_info();
    int dock_w = 584;
    int dock_x = ((int)f->width - dock_w) / 2;
    int dock_y = (int)f->height - 62;
    int x = dock_x + 7 + index * 96;
    return mouse_x_pos >= x && mouse_x_pos < x + 88 &&
           mouse_y_pos >= dock_y + 6 && mouse_y_pos < dock_y + 48;
}

static int point_in_notification_bell(void) {
    const framebuffer_info_t* f = framebuffer_info();
    int bx = (int)f->width - 32;
    return mouse_x_pos >= bx - 14 && mouse_x_pos < bx + 9 &&
           mouse_y_pos >= 8 && mouse_y_pos < 39;
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
    if (row == 0) return mouse_x_pos >= w->x + 8 && mouse_x_pos < w->x + w->w - 8 &&
                         mouse_y_pos >= y - 8 && mouse_y_pos < y + 45;
    y += 66;
    if (row == 1) return mouse_x_pos >= w->x + 8 && mouse_x_pos < w->x + w->w - 8 &&
                         mouse_y_pos >= y - 8 && mouse_y_pos < y + 36;
    y += 48;
    return mouse_x_pos >= w->x + 8 && mouse_x_pos < w->x + w->w - 8 &&
           mouse_y_pos >= y - 8 && mouse_y_pos < y + 36;
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
    notification_open = 1;
    power_open = 0;
    gui_strcopy(calc_display, "0", (int)sizeof(calc_display));
    calc_has_value = 0;
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
}

void gui_present(void) {
    if (!ready) return;
    if (dirty) {
        int px = dirty_x0, py = dirty_y0;
        int pw = dirty_x1 - dirty_x0, ph = dirty_y1 - dirty_y0;
        if (cursor_drawn_valid)
            renderer_present_rect(cursor_drawn_x, cursor_drawn_y, 7, 17);
        gui_redraw();
        /* The backbuffer is rebuilt completely, but the physical framebuffer
         * only needs the region that actually changed. This avoids multi-MB
         * uncached writes for every keypress or small GUI interaction. */
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
    if (control == 1) {
        w->minimized = 1;
        w->dragging = 0;
        if (focused_window == index) focused_window = -1;
        return;
    }
    if (control == 2) {
        w->visible = 0;
        w->minimized = 0;
        w->dragging = 0;
        if (focused_window == index) focused_window = -1;
    }
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
    mouse_y_pos += dy; /* mouse driver normalizes PS/2 Y to screen coordinates */
    mouse_x_pos = max_int(0, min_int(mouse_x_pos, (int)f->width - 1));
    mouse_y_pos = max_int(0, min_int(mouse_y_pos, (int)f->height - 1));

    int old_dock_hot = dock_hot;
    dock_hot = -1;
    for (int i = 0; i < 6; ++i) {
        if (point_in_dock(i)) { dock_hot = i; break; }
    }
    if (dock_hot != old_dock_hot) gui_request_redraw();

    if (wheel != 0 && inside_window(&windows[0], mouse_x_pos, mouse_y_pos)) {
        int delta = wheel > 0 ? -3 : 3;
        int max_top = max_int(0, TERM_ROWS - term_view_rows);
        term_view_top = max_int(0, min_int(term_view_top + delta, max_top));
        gui_request_redraw();
    }

    uint8_t pressed = (uint8_t)((new_buttons ^ mouse_prev_buttons) & new_buttons);
    if (pressed & 1u) {
        /* Top bar notification bell. */
        if (point_in_notification_bell()) {
            notification_open = !notification_open;
            power_open = 0;
            gui_request_redraw();
        }
        /* Power popup buttons. */
        else if (point_in_power_button(0)) {
            action_push(GUI_ACTION_REBOOT, 0);
            power_open = 0;
        }
        else if (point_in_power_button(1)) {
            action_push(GUI_ACTION_SHUTDOWN, 0);
            power_open = 0;
        }
        /* Dock. */
        else if (dock_hot >= 0) {
            power_open = dock_hot == 5 ? !power_open : 0;
            if (dock_hot < 5) {
                int idx = dock_hot;
                windows[idx].visible = 1;
                windows[idx].minimized = 0;
                promote_window(idx);
            }
            gui_request_redraw();
        }
        else {
            /* Focus topmost eligible window. */
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
                    if (mouse_y_pos >= w->y + TITLE_H && mouse_y_pos < w->y + w->h)
                        w->dragging = 0;
                } else if (hit == 3) {
                    int key_index = -1;
                    if (point_in_calculator_key(&key_index)) calculator_key(key_index);
                    else {
                        const struct gui_window* cw = &windows[3];
                        int clear_y = cw->y + TITLE_H + 18 + 68 + 4 * (48 + 8);
                        if (mouse_x_pos >= cw->x + 18 && mouse_x_pos < cw->x + 18 + 4 * 70 + 3 * 8 &&
                            mouse_y_pos >= clear_y && mouse_y_pos < clear_y + 34) {
                            gui_strcopy(calc_display, "0", (int)sizeof(calc_display));
                            calc_has_value = 0;
                            gui_request_redraw();
                        }
                    }
                } else if (hit == 2) {
                    if (point_in_settings_row(0)) {
                        action_push(GUI_ACTION_THEME, 0);
                    } else if (point_in_settings_row(1)) {
                        notifications_enabled = !notifications_enabled;
                    } else if (point_in_settings_row(2)) {
                        animations_enabled = !animations_enabled;
                    }
                }
                if (mouse_y_pos < w->y + TITLE_H && !control) {
                    w->dragging = 1;
                    w->drag_dx = mouse_x_pos - w->x;
                    w->drag_dy = mouse_y_pos - w->y;
                }
            } else {
                focused_window = -1;
                power_open = 0;
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
        gui_request_redraw_rect(x0, y0, x1 - x0, y1 - y0);
    }

    mouse_prev_buttons = new_buttons;
}

void gui_set_runtime_ticks(uint32_t ticks) {
    if (!ready || (ticks % 25u) != 0u) return;
    if (runtime_ticks != ticks) {
        runtime_ticks = ticks;
        if (windows[4].visible && !windows[4].minimized)
            gui_request_redraw_rect(windows[4].x, windows[4].y, windows[4].w, windows[4].h);
    }
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
