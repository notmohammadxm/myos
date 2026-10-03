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
#define MIN_WINDOW_W 260
#define MIN_WINDOW_H 180
#define WINDOW_COUNT 5
#define TITLE_DOUBLE_CLICK_TICKS 35u
#define SNAP_ZONE 24
#define RESIZE_ZONE 14
#define ANIMATION_TICKS 2u
#define GUI_KEY_CTRL_L 0x8B
#define GUI_KEY_UP 0x80
#define GUI_KEY_DOWN 0x81
#define GUI_KEY_LEFT 0x82
#define GUI_KEY_RIGHT 0x83
#define GUI_KEY_DELETE 0x84
#define GUI_KEY_ESCAPE 0x8C
#define DOCK_W 700
#define DOCK_H 64
#define DOCK_CELL_W 112
#define DOCK_Y_FROM_BOTTOM 76
#define LAUNCHER_W 360
#define LAUNCHER_H 330
#define QUICK_W 300
#define QUICK_H 250
struct gui_window {
    int x, y, w, h;
    int target_x, target_y, target_w, target_h;
    int restore_x, restore_y, restore_w, restore_h;
    int dragging;
    int resizing;
    int resize_edges;
    int resize_anchor_right;
    int resize_anchor_bottom;
    int drag_dx, drag_dy;
    int visible;
    int minimized;
    int maximized;
    int minimizing;
    int closing;
    int opening;
    int snap_state;
    int hover_control;
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
static int mouse_capture_window = -1;
static int mouse_capture_mode;
static int last_title_click_window = -1;
static uint32_t last_title_click_tick;
static uint32_t runtime_ticks;
static int clock_hour;
static int clock_minute;
static int clock_second;
static int clock_day;
static int clock_month;
static int clock_year;
static int clock_valid;
static int dock_hot = -1;
static int dock_hover_anim[6];
static int launcher_open;
static int launcher_selected;
static int launcher_query_len;
static char launcher_query[32];
static int recent_apps[5] = { 0, 1, 2, 3, 4 };
static int quick_settings_open;
static int notification_open = 1;
static int notifications_enabled = 1;
static int notifications_cleared;
static int animations_enabled = 1;
static int power_open;
static int taskmgr_open;
static int taskmgr_selected;
static int taskmgr_sort_mode;
static gui_task_info_t task_data[8];
static int task_count;
static int task_order[8];
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
static void gui_request_redraw_rect(int x, int y, int w, int h);
static void gui_request_terminal_redraw(void);
static struct gui_window windows[WINDOW_COUNT];

static int min_int(int a, int b) { return a < b ? a : b; }
static int max_int(int a, int b) { return a > b ? a : b; }
static size_t gui_strlen(const char* s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static void gui_strcopy(char* dst, const char* src, int cap) { int i = 0; if (!dst || cap <= 0) return; while (src && src[i] && i < cap - 1) { dst[i] = src[i]; ++i; } dst[i] = 0; }

static void action_push(int type, const char* value) {
    int next = (action_head + 1) % 4;
    if (next == action_tail) return;
    action_queue[action_head].type = type;
    action_queue[action_head].value = value;
    action_queue[action_head].arg = 0;
    action_head = next;
}

static void action_push_arg(int type, int arg) {
    int next = (action_head + 1) % 4;
    if (next == action_tail) return;
    action_queue[action_head].type = type;
    action_queue[action_head].value = 0;
    action_queue[action_head].arg = arg;
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

static void draw_text_clipped(int x, int y, const char* s, uint32_t color, int max_width) {
    if (!s || max_width < FONT_W) return;
    int max_chars = max_width / FONT_W;
    int len = (int)gui_strlen(s);
    if (len <= max_chars) {
        draw_text(x, y, s, color);
        return;
    }
    if (max_chars <= 3) {
        for (int i = 0; i < max_chars; ++i)
            draw_char(x + i * FONT_W, y, s[i], color);
        return;
    }
    for (int i = 0; i < max_chars - 3; ++i)
        draw_char(x + i * FONT_W, y, s[i], color);
    draw_text(x + (max_chars - 3) * FONT_W, y, "...", color);
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
    } else if (theme_name[0] == 'l' && theme_name[1] == 'i') {
        bg = framebuffer_make_color(237, 241, 247);
        panel = framebuffer_make_color(255, 255, 255);
        panel2 = framebuffer_make_color(248, 250, 253);
        terminal_bg = framebuffer_make_color(246, 248, 251);
        accent = framebuffer_make_color(42, 112, 224);
        text = framebuffer_make_color(25, 31, 42);
        muted = framebuffer_make_color(94, 104, 120);
        border = framebuffer_make_color(202, 209, 220);
        success = framebuffer_make_color(34, 145, 84);
        warning = framebuffer_make_color(191, 126, 24);
        danger = framebuffer_make_color(207, 58, 73);
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

static void clamp_target(struct gui_window* w) {
    const framebuffer_info_t* f = framebuffer_info();
    if (!f) return;
    int max_w = max_int(MIN_WINDOW_W, (int)f->width - 8);
    int max_h = max_int(MIN_WINDOW_H, (int)f->height - TOPBAR_H - FOOTER_H - 8);
    if (w->target_w < MIN_WINDOW_W) w->target_w = MIN_WINDOW_W;
    if (w->target_h < MIN_WINDOW_H) w->target_h = MIN_WINDOW_H;
    if (w->target_w > max_w) w->target_w = max_w;
    if (w->target_h > max_h) w->target_h = max_h;
    int max_x = (int)f->width - w->target_w - 4;
    int max_y = (int)f->height - FOOTER_H - w->target_h - 4;
    if (max_x < 4) max_x = 4;
    if (max_y < TOPBAR_H + 2) max_y = TOPBAR_H + 2;
    if (w->target_x < 4) w->target_x = 4;
    if (w->target_y < TOPBAR_H + 2) w->target_y = TOPBAR_H + 2;
    if (w->target_x > max_x) w->target_x = max_x;
    if (w->target_y > max_y) w->target_y = max_y;
}

static void window_set_initial(struct gui_window* w, int x, int y, int width, int height) {
    w->target_x = x;
    w->target_y = y;
    w->target_w = width;
    w->target_h = height;
    clamp_target(w);
    w->x = w->target_x;
    w->y = w->target_y;
    w->w = w->target_w;
    w->h = w->target_h;
    w->restore_x = w->x;
    w->restore_y = w->y;
    w->restore_w = w->w;
    w->restore_h = w->h;
}

static void dock_geometry(int index, int* x, int* y, int* w, int* h) {
    const framebuffer_info_t* f = framebuffer_info();
    int dock_x = f ? ((int)f->width - DOCK_W) / 2 : 0;
    int dock_y = f ? (int)f->height - DOCK_Y_FROM_BOTTOM : 0;
    if (x) *x = dock_x + 16 + index * DOCK_CELL_W;
    if (y) *y = dock_y + 14;
    if (w) *w = 84;
    if (h) *h = 34;
}

static int smooth_value(int current, int target) {
    int diff = target - current;
    if (diff == 0) return current;
    if (diff > -2 && diff < 2) return target;
    int step = (diff * 55) / 100;
    if (step == 0) step = diff > 0 ? 1 : -1;
    return current + step;
}

static void window_finish_transition(struct gui_window* w) {
    w->x = w->target_x;
    w->y = w->target_y;
    w->w = w->target_w;
    w->h = w->target_h;
    if (w->minimizing) {
        w->minimized = 1;
        w->minimizing = 0;
    }
    if (w->closing) {
        w->visible = 0;
        w->closing = 0;
    }
    w->opening = 0;
}

static void window_set_target(struct gui_window* w, int x, int y, int width, int height) {
    w->target_x = x;
    w->target_y = y;
    w->target_w = width;
    w->target_h = height;
    clamp_target(w);
    if (!animations_enabled) window_finish_transition(w);
}

static void window_transition_rect(struct gui_window* w, int old_x, int old_y, int old_w, int old_h) {
    int x0 = old_x < w->x ? old_x : w->x;
    int y0 = old_y < w->y ? old_y : w->y;
    int x1 = (old_x + old_w) > (w->x + w->w) ? (old_x + old_w) : (w->x + w->w);
    int y1 = (old_y + old_h) > (w->y + w->h) ? (old_y + old_h) : (w->y + w->h);
    int tx1 = w->target_x + w->target_w;
    int ty1 = w->target_y + w->target_h;
    if (w->target_x < x0) x0 = w->target_x;
    if (w->target_y < y0) y0 = w->target_y;
    if (tx1 > x1) x1 = tx1;
    if (ty1 > y1) y1 = ty1;
    gui_request_redraw_rect(x0 - 10, y0 - 10, x1 - x0 + 20, y1 - y0 + 20);
}

static void advance_window_animations(void) {
    for (int i = 0; i < WINDOW_COUNT; ++i) {
        struct gui_window* w = &windows[i];
        if (!w->visible && !w->opening) continue;
        if (w->x == w->target_x && w->y == w->target_y &&
            w->w == w->target_w && w->h == w->target_h) {
            if (w->minimizing || w->closing || w->opening) window_finish_transition(w);
            continue;
        }
        int old_x = w->x, old_y = w->y, old_w = w->w, old_h = w->h;
        w->x = smooth_value(w->x, w->target_x);
        w->y = smooth_value(w->y, w->target_y);
        w->w = smooth_value(w->w, w->target_w);
        w->h = smooth_value(w->h, w->target_h);
        if (w->x == w->target_x && w->y == w->target_y &&
            w->w == w->target_w && w->h == w->target_h) window_finish_transition(w);
        if (old_x != w->x || old_y != w->y || old_w != w->w || old_h != w->h)
            window_transition_rect(w, old_x, old_y, old_w, old_h);
    }
}

static void layout_windows(void) {
    const framebuffer_info_t* f = framebuffer_info();
    int screen_w = (int)f->width;
    int screen_h = (int)f->height;
    int side_w = 334;
    int gap = 14;

    window_set_initial(&windows[0], 24, TOPBAR_H + 30,
                       screen_w - side_w - 66, screen_h - TOPBAR_H - FOOTER_H - 42);
    window_set_initial(&windows[1], screen_w - side_w - 24, TOPBAR_H + 30, side_w, 238);
    window_set_initial(&windows[2], screen_w - side_w - 24,
                       TOPBAR_H + 30 + 238 + gap, side_w, 238);
    window_set_initial(&windows[3], 228, TOPBAR_H + 78, 390, 410);
    window_set_initial(&windows[4], max_int(360, screen_w / 2 + 40),
                       TOPBAR_H + 100, 390, 410);

    for (int i = 0; i < WINDOW_COUNT; ++i) {
        windows[i].visible = (i < 3) ? 1 : 0;
        windows[i].minimized = 0;
        windows[i].dragging = 0;
        windows[i].resizing = 0;
        windows[i].resize_edges = 0;
        windows[i].maximized = 0;
        windows[i].minimizing = 0;
        windows[i].closing = 0;
        windows[i].opening = 0;
        windows[i].snap_state = 0;
        windows[i].hover_control = 0;
    }
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

static uint32_t control_color(const struct gui_window* w, int control);
static void draw_control_button(const struct gui_window* w, int center_x, int control, uint32_t color);
static void draw_resize_grip(const struct gui_window* w, uint32_t color);

static void draw_round_rect(int x, int y, int width, int height, int radius, uint32_t color) {
    if (width <= 0 || height <= 0) return;
    if (radius <= 0) { renderer_rect(x, y, width, height, color); return; }
    if (radius * 2 > width) radius = width / 2;
    if (radius * 2 > height) radius = height / 2;
    int rr = radius * radius;
    for (int py = 0; py < height; ++py) {
        int inset = 0;
        if (py < radius) {
            int dy = radius - 1 - py;
            int dx = 0;
            while ((dx + 1) * (dx + 1) + dy * dy <= rr) ++dx;
            inset = radius - 1 - dx;
        } else if (py >= height - radius) {
            int dy = py - (height - radius);
            int dx = 0;
            while ((dx + 1) * (dx + 1) + dy * dy <= rr) ++dx;
            inset = radius - 1 - dx;
        }
        int span = width - inset * 2;
        if (span > 0) renderer_rect(x + inset, y + py, span, 1, color);
    }
}

static void draw_round_card(int x, int y, int width, int height, int radius,
                            uint32_t fill, uint32_t line) {
    draw_round_rect(x, y, width, height, radius, line);
    if (width > 2 && height > 2)
        draw_round_rect(x + 1, y + 1, width - 2, height - 2, radius - 1, fill);
}

static void draw_text_centered(int x, int y, int width, const char* s, uint32_t color) {
    if (!s) return;
    int tw = (int)gui_strlen(s) * FONT_W;
    draw_text(x + max_int(0, (width - tw) / 2), y, s, color);
}

static void draw_soft_divider(int x, int y, int width, uint32_t color) {
    if (width <= 0) return;
    renderer_rect(x, y, width, 1, color);
    renderer_rect(x, y + 1, width, 1, bg);
}

static void draw_window_glow(const struct gui_window* w, int active) {
    if (!w || w->w <= 0 || w->h <= 0) return;
    uint32_t black = framebuffer_make_color(0, 0, 0);
    draw_round_rect(w->x + 6, w->y + 10, w->w, w->h, 16, black);
    if (active && animations_enabled) {
        draw_round_rect(w->x + 2, w->y + 2, w->w - 4, 3, 2, accent);
    }
}

static void draw_window_controls_modern(const struct gui_window* w) {
    if (!w || w->w < 170) return;
    draw_control_button(w, w->x + w->w - 90, 1, control_color(w, 1));
    draw_control_button(w, w->x + w->w - 60, 2, control_color(w, 2));
    draw_control_button(w, w->x + w->w - 30, 3, control_color(w, 3));
}

static uint32_t control_color(const struct gui_window* w, int control) {
    if (w->hover_control == control) {
        if (control == 3) return danger;
        if (control == 2) return accent;
        return warning;
    }
    return border;
}

static void draw_control_button(const struct gui_window* w, int center_x, int control, uint32_t color) {
    draw_round_card(center_x - 11, w->y + 10, 22, 20, 7,
                    w->hover_control == control ? panel2 : panel, color);
    int cy = w->y + 20;
    if (control == 1) {
        renderer_rect(center_x - 5, cy + 3, 10, 2, color);
    } else if (control == 2) {
        renderer_border(center_x - 5, cy - 5, 10, 10, 1, color);
    } else {
        renderer_line(center_x - 4, cy - 4, center_x + 4, cy + 4, color);
        renderer_line(center_x + 4, cy - 4, center_x - 4, cy + 4, color);
    }
}

static void draw_resize_grip(const struct gui_window* w, uint32_t color) {
    if (!w || w->maximized || w->resizing || w->minimizing || w->closing ||
        w->w < MIN_WINDOW_W || w->h < MIN_WINDOW_H) return;
    int x = w->x + w->w - 18;
    int y = w->y + w->h - 18;
    draw_round_rect(x + 6, y + 8, 3, 3, 1, color);
    draw_round_rect(x + 10, y + 4, 3, 3, 1, color);
    draw_round_rect(x + 10, y + 10, 3, 3, 1, color);
}

static void draw_window(const struct gui_window* w, const char* title, int index) {
    if (!w->visible || w->w < 80 || w->h < 30) return;
    int active = index == focused_window;
    uint32_t line = active ? accent : border;
    if (w->minimizing || w->closing) line = muted;

    draw_window_glow(w, active);
    draw_round_card(w->x, w->y, w->w, w->h, 14, panel, line);

    if (w->w > 4 && w->h > TITLE_H + 2) {
        draw_round_rect(w->x + 2, w->y + 2, w->w - 4, TITLE_H + 7, 11, panel2);
        renderer_rect(w->x + 13, w->y + TITLE_H - 3, w->w - 26, 7, panel2);
        renderer_rect(w->x + 14, w->y + 2, w->w - 28, 2, line);
    }

    draw_soft_divider(w->x + 14, w->y + TITLE_H + 1, w->w - 28, border);
    draw_text_clipped(w->x + 24, w->y + 12, title, active ? text : muted, w->w - 140);
    if (active) {
        draw_round_rect(w->x + 14, w->y + 29, 7, 7, 3, accent);
        draw_text(w->x + 28, w->y + 27, "ACTIVE", accent);
    }
    draw_window_controls_modern(w);
    draw_resize_grip(w, active ? accent : muted);
}

static void draw_sysinfo(const struct gui_window* w) {
    if (!w->visible || w->minimized || w->w < 160 || w->h < TITLE_H + 60) return;
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

    /* Subtle desktop depth without a separate compositing layer. */
    renderer_rect(0, TOPBAR_H + 1, w, 1, border);
    renderer_rect(0, h - FOOTER_H, w, 1, border);

    draw_round_card(10, 9, w - 20, 46, 16, panel2, border);
    renderer_rect(28, 14, 96, 2, accent);

    draw_round_card(18, 15, 46, 34, 12, launcher_open ? panel : bg, launcher_open ? accent : border);
    draw_round_rect(28, 22, 26, 20, 7, launcher_open ? accent : muted);
    draw_round_rect(34, 27, 14, 2, 1, launcher_open ? bg : panel2);
    draw_round_rect(34, 31, 10, 2, 1, launcher_open ? bg : panel2);
    draw_round_rect(34, 35, 14, 2, 1, launcher_open ? bg : panel2);

    draw_text(78, 18, "MYOS", accent);
    draw_text(126, 18, "DESKTOP", muted);

    draw_round_card(w - 536, 15, 86, 34, 12, bg, border);
    draw_round_rect(w - 524, 25, 6, 6, 3, success);
    draw_text(w - 512, 18, "NET", success);

    draw_round_card(w - 444, 15, 86, 34, 12, bg, border);
    draw_round_rect(w - 432, 25, 6, 6, 3, success);
    draw_text(w - 420, 18, "MOUSE", success);

    draw_round_card(w - 352, 15, 86, 34, 12, bg, border);
    draw_round_rect(w - 340, 25, 6, 6, 3, success);
    draw_text(w - 328, 18, "CPU", success);

    draw_round_card(w - 260, 15, 82, 34, 12, bg, border);
    draw_text_centered(w - 260, 18, 82, clock, text);
    if (clock_valid) {
        char date[12];
        date[0]=(char)('0'+(clock_day/10)%10); date[1]=(char)('0'+clock_day%10); date[2]='/';
        date[3]=(char)('0'+(clock_month/10)%10); date[4]=(char)('0'+clock_month%10); date[5]='/';
        date[6]=(char)('0'+(clock_year/1000)%10); date[7]=(char)('0'+(clock_year/100)%10);
        date[8]=(char)('0'+(clock_year/10)%10); date[9]=(char)('0'+clock_year%10); date[10]=0;
        draw_text_centered(w - 252, 35, 66, date, muted);
    }

    draw_round_card(w - 105, 15, 72, 34, 12,
                    notification_open ? panel : bg,
                    notification_open ? accent : border);
    draw_round_rect(w - 84, 23, 14, 14, 5, notification_open ? accent : muted);
    draw_round_rect(w - 86, 34, 18, 2, 1, notification_open ? accent : muted);

    /* Floating dock with animated hover lift. */
    int dock_x = (w - DOCK_W) / 2;
    int dock_y = h - DOCK_Y_FROM_BOTTOM;
    draw_round_card(dock_x - 2, dock_y - 2, DOCK_W + 4, DOCK_H + 4, 20, panel, border);
    renderer_rect(dock_x + 24, dock_y + 2, DOCK_W - 48, 1, bg);

    const char* names[6] = { "TERM", "SYSTEM", "SETTINGS", "CALC", "MONITOR", "POWER" };
    for (int i = 0; i < 6; ++i) {
        int hover = dock_hover_anim[i];
        int extra = (hover * 8) / 100;
        int cell_x = dock_x + 8 + i * DOCK_CELL_W - extra / 2;
        int cell_y = dock_y + 7 - extra / 3;
        int cell_w = DOCK_CELL_W - 6 + extra;
        int cell_h = DOCK_H - 14 + extra;
        uint32_t cc = hover > 20 ? accent : muted;
        draw_round_card(cell_x, cell_y, cell_w, cell_h, 12,
                        hover > 10 ? panel2 : bg, cc);
        int icon_x = cell_x + 12;
        int icon_y = cell_y + 8;
        if (i == 0) draw_icon_terminal(icon_x, icon_y, cc);
        if (i == 1) draw_icon_system(icon_x, icon_y, cc);
        if (i == 2) draw_icon_settings(icon_x, icon_y, cc);
        if (i == 3) draw_icon_calculator(icon_x, icon_y, cc);
        if (i == 4) draw_icon_monitor(icon_x, icon_y, cc);
        if (i == 5) draw_icon_power(icon_x, icon_y, cc);
        draw_text(cell_x + 40, cell_y + 10, names[i], cc);
    }

    draw_round_card(16, h - 49, 152, 30, 12, bg, border);
    draw_round_rect(28, h - 40, 6, 6, 3, success);
    draw_text(44, h - 43, "MYOS CORE", text);
    draw_text(112, h - 43, "READY", success);

    draw_round_card(w - 184, h - 49, 168, 30, 12, bg, border);
    draw_text(w - 168, h - 43, "FOCUS", muted);
    if (focused_window >= 0 && focused_window < WINDOW_COUNT) {
        const char* focus_name =
            focused_window == 0 ? "TERMINAL" :
            focused_window == 1 ? "SYSTEM" :
            focused_window == 2 ? "SETTINGS" :
            focused_window == 3 ? "CALC" : "MONITOR";
        draw_text(w - 116, h - 43, focus_name, accent);
    } else draw_text(w - 116, h - 43, "DESKTOP", text);
}

static int gui_ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

static int launcher_contains(const char* text_in, const char* query) {
    if (!query || !query[0]) return 1;
    if (!text_in) return 0;
    for (int i = 0; text_in[i]; ++i) {
        int j = 0;
        while (query[j] && text_in[i + j] &&
               gui_ascii_lower(text_in[i + j]) == gui_ascii_lower(query[j])) ++j;
        if (!query[j]) return 1;
    }
    return 0;
}

static int launcher_match_count(void) {
    const char* names[6] = {
        "TERMINAL", "SYSTEM INFORMATION", "SETTINGS", "CALCULATOR", "SYSTEM MONITOR",
        "TASK MANAGER"
    };
    const char* desc[6] = {
        "SHELL AND COMMAND CENTER", "HARDWARE AND DISPLAY", "THEME AND SESSION",
        "FAST INTEGER MATH", "LIVE RUNTIME STATUS", "PROCESSES AND RESOURCE CONTROL"
    };
    int count = 0;
    for (int pos = 0; pos < 6; ++pos) {
        int index = recent_apps[pos < 6 ? pos : 0];
        if (index < 0 || index >= 6) continue;
        if (launcher_contains(names[index], launcher_query) ||
            launcher_contains(desc[index], launcher_query)) ++count;
    }
    return count;
}

static int launcher_match_at(int ordinal) {
    const char* names[6] = {
        "TERMINAL", "SYSTEM INFORMATION", "SETTINGS", "CALCULATOR", "SYSTEM MONITOR",
        "TASK MANAGER"
    };
    const char* desc[6] = {
        "SHELL AND COMMAND CENTER", "HARDWARE AND DISPLAY", "THEME AND SESSION",
        "FAST INTEGER MATH", "LIVE RUNTIME STATUS", "PROCESSES AND RESOURCE CONTROL"
    };
    int match = 0;
    for (int pos = 0; pos < 6; ++pos) {
        int index = recent_apps[pos];
        if (index < 0 || index >= 6) continue;
        if (!launcher_contains(names[index], launcher_query) &&
            !launcher_contains(desc[index], launcher_query)) continue;
        if (match == ordinal) return index;
        ++match;
    }
    return -1;
}

static void open_launcher_app(int index) {
    if (index == 5) {
        gui_taskmgr_open();
        launcher_open = 0;
        launcher_reset_search();
        gui_request_redraw();
        return;
    }
    if (index >= 0 && index < WINDOW_COUNT) open_window(index);
}

static void launcher_reset_search(void) {
    launcher_query[0] = 0;
    launcher_query_len = 0;
    launcher_selected = 0;
}

static void launcher_toggle(void) {
    launcher_open = !launcher_open;
    if (launcher_open) launcher_reset_search();
    quick_settings_open = 0;
    power_open = 0;
    taskmgr_open = 0;
    gui_request_redraw_rect(0, 0, LAUNCHER_W + 20, TOPBAR_H + LAUNCHER_H + 18);
}

static void draw_launcher(void) {
    if (!launcher_open) return;
    int x = 18, y = TOPBAR_H + 8;
    int width = LAUNCHER_W, height = LAUNCHER_H;
    int matches = launcher_match_count();
    if (launcher_selected >= matches) launcher_selected = max_int(0, matches - 1);

    draw_round_card(x + 7, y + 11, width, height, 20, bg, border);
    draw_round_card(x, y, width, height, 20, panel2, accent);

    draw_text(x + 24, y + 18, "MYOS", accent);
    draw_text(x + 24, y + 38, launcher_query_len ? "SEARCH RESULTS" : "RECENT APPLICATIONS", muted);

    draw_round_card(x + 18, y + 64, width - 36, 38, 11, bg, border);
    draw_round_rect(x + 31, y + 76, 12, 12, 4, accent);
    draw_text(x + 53, y + 73, launcher_query_len ? launcher_query : "SEARCH APPLICATIONS",
              launcher_query_len ? text : muted);
    if (launcher_query_len) draw_text(x + 53 + launcher_query_len * FONT_W, y + 73, "_", accent);

    if (matches == 0) {
        draw_text_centered(x + 18, y + 130, width - 36, "NO APPLICATIONS FOUND", muted);
    } else {
        const char* names[6] = {
            "TERMINAL", "SYSTEM INFORMATION", "SETTINGS", "CALCULATOR", "SYSTEM MONITOR",
            "TASK MANAGER"
        };
        for (int ordinal = 0; ordinal < matches && ordinal < 5; ++ordinal) {
            int index = launcher_match_at(ordinal);
            int iy = y + 112 + ordinal * 40;
            int active = ordinal == launcher_selected;
            draw_round_card(x + 18, iy, width - 36, 34, 10,
                            active ? panel : bg, active ? accent : border);
            if (index == 0) draw_icon_terminal(x + 30, iy + 7, active ? accent : muted);
            else if (index == 1) draw_icon_system(x + 30, iy + 7, active ? accent : muted);
            else if (index == 2) draw_icon_settings(x + 30, iy + 7, active ? accent : muted);
            else if (index == 3) draw_icon_calculator(x + 30, iy + 7, active ? accent : muted);
            else if (index == 4) draw_icon_monitor(x + 30, iy + 7, active ? accent : muted);
            else draw_icon_system(x + 30, iy + 7, active ? accent : muted);
            draw_text_clipped(x + 64, iy + 5, names[index], active ? text : muted, width - 92);
        }
    }

    draw_text(x + 20, y + height - 22, "UP/DOWN SELECT   ENTER OPEN   ESC CLOSE", muted);
}

static void launcher_reset_search(void) {
    launcher_query[0] = 0;
    launcher_query_len = 0;
    launcher_selected = 0;
}

static void launcher_toggle(void) {
    launcher_open = !launcher_open;
    if (launcher_open) launcher_reset_search();
    quick_settings_open = 0;
    power_open = 0;
    gui_request_redraw_rect(0, 0, LAUNCHER_W + 20, TOPBAR_H + LAUNCHER_H + 18);
}

static void draw_launcher(void) {
    if (!launcher_open) return;
    int x = 18, y = TOPBAR_H + 8;
    int width = LAUNCHER_W, height = LAUNCHER_H;
    int matches = launcher_match_count();
    if (launcher_selected >= matches) launcher_selected = max_int(0, matches - 1);

    draw_round_card(x + 7, y + 11, width, height, 20, bg, border);
    draw_round_card(x, y, width, height, 20, panel2, accent);

    draw_text(x + 24, y + 18, "MYOS", accent);
    draw_text(x + 24, y + 38, launcher_query_len ? "SEARCH RESULTS" : "RECENT APPLICATIONS", muted);

    draw_round_card(x + 18, y + 64, width - 36, 38, 11, bg, border);
    draw_round_rect(x + 31, y + 76, 12, 12, 4, accent);
    draw_text(x + 53, y + 73, launcher_query_len ? launcher_query : "SEARCH APPLICATIONS",
              launcher_query_len ? text : muted);
    if (launcher_query_len) draw_text(x + 53 + launcher_query_len * FONT_W, y + 73, "_", accent);

    if (matches == 0) {
        draw_text_centered(x + 18, y + 130, width - 36, "NO APPLICATIONS FOUND", muted);
    } else {
        for (int ordinal = 0; ordinal < matches && ordinal < 5; ++ordinal) {
            int index = launcher_match_at(ordinal);
            int iy = y + 112 + ordinal * 40;
            int active = ordinal == launcher_selected;
            draw_round_card(x + 18, iy, width - 36, 34, 10,
                            active ? panel : bg, active ? accent : border);
            if (index == 0) draw_icon_terminal(x + 30, iy + 7, active ? accent : muted);
            else if (index == 1) draw_icon_system(x + 30, iy + 7, active ? accent : muted);
            else if (index == 2) draw_icon_settings(x + 30, iy + 7, active ? accent : muted);
            else if (index == 3) draw_icon_calculator(x + 30, iy + 7, active ? accent : muted);
            else draw_icon_monitor(x + 30, iy + 7, active ? accent : muted);
            const char* names[5] = {
                "TERMINAL", "SYSTEM INFORMATION", "SETTINGS", "CALCULATOR", "SYSTEM MONITOR"
            };
            draw_text(x + 64, iy + 5, names[index], active ? text : muted);
        }
    }

    draw_text(x + 20, y + height - 22, "UP/DOWN SELECT   ENTER OPEN   ESC CLOSE", muted);
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
    int width = 326, height = 144;
    int x = (int)f->width - width - 22;
    int y = (int)f->height - FOOTER_H - height - 22;
    draw_round_card(x + 5, y + 8, width, height, 18, bg, border);
    draw_round_card(x, y, width, height, 18, panel2, accent);
    draw_round_rect(x + 14, y + 16, 7, height - 62, 3, accent);
    draw_text(x + 34, y + 14, "NOTIFICATIONS", accent);
    if (notifications_cleared) {
        draw_text(x + 34, y + 44, "ALL CLEAR", text);
        draw_text(x + 34, y + 66, "NO ACTIVE NOTIFICATIONS", muted);
    } else {
        draw_round_rect(x + 34, y + 41, 6, 6, 3, success);
        draw_text(x + 48, y + 37, "MYOS BOOT COMPLETED", text);
        draw_round_rect(x + 34, y + 67, 6, 6, 3, accent);
        draw_text(x + 48, y + 63, "GUI SERVICES READY", muted);
    }
    draw_round_card(x + 18, y + 100, width - 36, 30, 10, bg, border);
    draw_text_centered(x + 18, y + 107, width - 36, "CLEAR ALL", muted);
}
static int quick_settings_x(void) {
    const framebuffer_info_t* f = framebuffer_info();
    return f ? (int)f->width - QUICK_W - 18 : 0;
}

static int quick_settings_y(void) {
    return TOPBAR_H + 6;
}

static void draw_quick_settings(void) {
    if (!quick_settings_open) return;
    const int x = quick_settings_x(), y = quick_settings_y();
    draw_round_card(x + 5, y + 8, QUICK_W, QUICK_H, 18, bg, border);
    draw_round_card(x, y, QUICK_W, QUICK_H, 18, panel2, accent);
    draw_text(x + 20, y + 16, "QUICK SETTINGS", accent);
    draw_text(x + 20, y + 38, "SYSTEM CONTROLS", muted);

    draw_round_card(x + 16, y + 62, QUICK_W - 32, 38, 11, bg, border);
    draw_text(x + 30, y + 73, "THEME", muted);
    draw_text(x + 150, y + 73, theme_name, text);
    draw_text(x + QUICK_W - 46, y + 73, ">", accent);

    draw_round_card(x + 16, y + 106, QUICK_W - 32, 38, 11, bg, border);
    draw_text(x + 30, y + 117, "NOTIFICATIONS", muted);
    draw_text(x + 206, y + 117, notifications_enabled ? "ON" : "OFF",
              notifications_enabled ? success : muted);

    draw_round_card(x + 16, y + 150, QUICK_W - 32, 38, 11, bg, border);
    draw_text(x + 30, y + 161, "ANIMATIONS", muted);
    draw_text(x + 206, y + 161, animations_enabled ? "ON" : "OFF",
              animations_enabled ? success : muted);

    draw_round_card(x + 16, y + 194, QUICK_W - 32, 38, 11, accent, accent);
    draw_text_centered(x + 16, y + 205, QUICK_W - 32, "OPEN SYSTEM MONITOR", bg);
}

static void draw_task_manager(void) {
    if (!taskmgr_open) return;
    const framebuffer_info_t* f = framebuffer_info();
    if (!f) return;
    int width = 720, height = 430;
    if ((int)f->width < width + 24) width = (int)f->width - 24;
    if ((int)f->height < height + TOPBAR_H + FOOTER_H) height = (int)f->height - TOPBAR_H - FOOTER_H - 24;
    int x = ((int)f->width - width) / 2;
    int y = TOPBAR_H + ((int)f->height - TOPBAR_H - FOOTER_H - height) / 2;

    draw_round_card(x + 7, y + 11, width, height, 20, bg, border);
    draw_round_card(x, y, width, height, 20, panel2, accent);
    draw_text(x + 22, y + 18, "TASK MANAGER", accent);
    draw_text(x + 22, y + 40, "ROUND ROBIN KERNEL TASKS", muted);

    const char* headers[6] = { "PID", "NAME", "STATE", "CPU", "MEM", "PRIO" };
    int cols[6] = { 24, 72, 286, 490, 566, 640 };
    for (int i = 0; i < 6; ++i) draw_text(x + cols[i], y + 72, headers[i], muted);
    draw_soft_divider(x + 20, y + 94, width - 40, border);

    for (int row = 0; row < task_count && row < 8; ++row) {
        int oi = task_order[row];
        if (oi < 0 || oi >= task_count) continue;
        int yy = y + 102 + row * 34;
        int active = row == taskmgr_selected;
        draw_round_card(x + 18, yy - 2, width - 36, 30, 9, active ? panel : bg, active ? accent : border);
        draw_text(x + cols[0], yy + 5, "00", active ? text : muted);
        int pid = task_data[oi].pid;
        draw_text(x + cols[0], yy + 5, pid < 10 ? (pid == 0 ? "00" : pid == 1 ? "01" : "02") : "??",
                  active ? text : muted);
        draw_text_clipped(x + cols[1], yy + 5, task_data[oi].name, active ? text : muted, 200);
        draw_text(x + cols[2], yy + 5, task_data[oi].state, active ? text : muted);
        draw_text(x + cols[3], yy + 5, task_data[oi].cpu_percent > 99 ? "99%" :
                  task_data[oi].cpu_percent > 9 ? (task_data[oi].cpu_percent == 10 ? "10%" : "??") : "0%", success);
        draw_text(x + cols[4], yy + 5, task_data[oi].memory_kib > 99 ? "999K" :
                  task_data[oi].memory_kib > 9 ? "64K" : "8K", text);
        draw_text(x + cols[5], yy + 5, task_data[oi].priority > 9 ? "9" :
                  task_data[oi].priority == 0 ? "0" : "1", warning);
    }

    draw_text(x + 20, y + height - 48, "UP/DOWN SELECT   C CPU   M MEMORY   N NAME   X TERMINATE   R RESTART   ESC CLOSE", muted);
}

static void draw_power_menu(void) {
    if (!power_open) return;
    const framebuffer_info_t* f = framebuffer_info();
    int width = 250, height = 124;
    int x = (int)f->width - width - 22;
    int y = (int)f->height - FOOTER_H - height - 22;
    draw_round_card(x + 4, y + 7, width, height, 18, bg, border);
    draw_round_card(x, y, width, height, 18, panel2, accent);
    draw_text(x + 18, y + 14, "POWER", accent);
    draw_round_card(x + 14, y + 46, 104, 58, 14, bg, warning);
    draw_round_card(x + 132, y + 46, 104, 58, 14, bg, danger);
    draw_text_centered(x + 14, y + 68, 104, "REBOOT", warning);
    draw_text_centered(x + 132, y + 68, 104, "SHUTDOWN", danger);
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
    int top = TOPBAR_H + 120;
    if (mouse_x_pos < 36 || mouse_x_pos >= 18 + LAUNCHER_W - 18) return 0;
    if (mouse_y_pos < top || mouse_y_pos >= top + 5 * 40) return 0;
    int ordinal = (mouse_y_pos - top) / 40;
    if (ordinal < 0 || ordinal >= launcher_match_count()) return 0;
    if ((mouse_y_pos - top) % 40 >= 34) return 0;
    *item = launcher_match_at(ordinal);
    return *item >= 0;
}

static int point_in_system_tray(void) {
    const framebuffer_info_t* f = framebuffer_info();
    if (!f) return 0;
    int x = (int)f->width;
    return mouse_x_pos >= x - 546 && mouse_x_pos < x - 268 &&
           mouse_y_pos >= 8 && mouse_y_pos < 54;
}

static int point_in_clock_capsule(void) {
    const framebuffer_info_t* f = framebuffer_info();
    if (!f) return 0;
    int x = (int)f->width - 260;
    return mouse_x_pos >= x && mouse_x_pos < x + 82 &&
           mouse_y_pos >= 8 && mouse_y_pos < 54;
}

static int point_in_quick_settings_item(int row) {
    if (!quick_settings_open) return 0;
    const int x = quick_settings_x(), y = quick_settings_y();
    if (mouse_x_pos < x + 10 || mouse_x_pos >= x + QUICK_W - 10) return 0;
    if (row == 0) return mouse_y_pos >= y + 56 && mouse_y_pos < y + 104;
    if (row == 1) return mouse_y_pos >= y + 104 && mouse_y_pos < y + 148;
    if (row == 2) return mouse_y_pos >= y + 148 && mouse_y_pos < y + 192;
    if (row == 3) return mouse_y_pos >= y + 192 && mouse_y_pos < y + 238;
    return 0;
}

static int point_in_notification_clear(void) {
    if (!notification_open || !notifications_enabled) return 0;
    const framebuffer_info_t* f = framebuffer_info();
    if (!f) return 0;
    int width = 326, height = 144;
    int x = (int)f->width - width - 22;
    int y = (int)f->height - FOOTER_H - height - 22;
    return mouse_x_pos >= x + 18 && mouse_x_pos < x + width - 18 &&
           mouse_y_pos >= y + 100 && mouse_y_pos < y + 130;
}

static int title_control_at(const struct gui_window* w) {
    if (!w || w->w < 150) return 0;
    int rel_x = mouse_x_pos - w->x;
    if (mouse_y_pos < w->y || mouse_y_pos >= w->y + TITLE_H) return 0;
    if (rel_x >= w->w - 38) return 3;
    if (rel_x >= w->w - 68) return 2;
    if (rel_x >= w->w - 98) return 1;
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
    struct gui_window* w = &windows[index];
    if (index < 5) {
        int pos = 0;
        while (pos < 5 && recent_apps[pos] != index) ++pos;
        if (pos >= 5) pos = 4;
        while (pos > 0) {
            recent_apps[pos] = recent_apps[pos - 1];
            --pos;
        }
        recent_apps[0] = index;
    }
    if (!w->visible) {
        int dx, dy, dw, dh;
        dock_geometry(index, &dx, &dy, &dw, &dh);
        w->x = dx;
        w->y = dy;
        w->w = dw;
        w->h = dh;
        w->visible = 1;
        w->minimized = 0;
        w->opening = 1;
        w->closing = 0;
        w->minimizing = 0;
    } else if (w->minimized) {
        w->minimized = 0;
        w->opening = 1;
        w->target_x = w->restore_x;
        w->target_y = w->restore_y;
        w->target_w = w->restore_w;
        w->target_h = w->restore_h;
    }
    promote_window(index);
    launcher_open = 0;
    power_open = 0;
    if (!animations_enabled) window_finish_transition(w);
    else window_transition_rect(w, w->x, w->y, w->w, w->h);
    gui_request_redraw_rect(w->x - 12, w->y - 12, w->w + 24, w->h + 24);
}

void gui_open_window(int index) {
    if (!ready) return;
    open_window(index);
}

static void focus_next_visible(void) {
    if (focused_window >= 0 && focused_window < WINDOW_COUNT &&
        windows[focused_window].visible && !windows[focused_window].minimized &&
        !windows[focused_window].closing) return;
    for (int i = WINDOW_COUNT - 1; i >= 0; --i) {
        int idx = window_order[i];
        if (windows[idx].visible && !windows[idx].minimized && !windows[idx].closing) {
            focused_window = idx;
            return;
        }
    }
    focused_window = -1;
}

static void finish_all_window_animations(void) {
    for (int i = 0; i < WINDOW_COUNT; ++i) {
        struct gui_window* w = &windows[i];
        if (w->x != w->target_x || w->y != w->target_y ||
            w->w != w->target_w || w->h != w->target_h ||
            w->minimizing || w->closing || w->opening) {
            int old_x = w->x, old_y = w->y, old_w = w->w, old_h = w->h;
            window_finish_transition(w);
            gui_request_redraw_rect(old_x - 12, old_y - 12, old_w + 24, old_h + 24);
            gui_request_redraw_rect(w->x - 12, w->y - 12, w->w + 24, w->h + 24);
        }
    }
}

static int gui_keyboard_event_internal(int event, int ctrl, int shift) {
    (void)shift;
    if (!ready) return 0;

    if (launcher_open) {
        int matches = launcher_match_count();
        if (event == GUI_KEY_ESCAPE) {
            launcher_open = 0;
            launcher_reset_search();
            gui_request_redraw_rect(0, 0, LAUNCHER_W + 20, TOPBAR_H + LAUNCHER_H + 18);
            return 1;
        }
        if (event == GUI_KEY_UP) {
            if (matches > 0) launcher_selected = (launcher_selected + matches - 1) % matches;
            gui_request_redraw_rect(18, TOPBAR_H + 8, LAUNCHER_W, LAUNCHER_H);
            return 1;
        }
        if (event == GUI_KEY_DOWN) {
            if (matches > 0) launcher_selected = (launcher_selected + 1) % matches;
            gui_request_redraw_rect(18, TOPBAR_H + 8, LAUNCHER_W, LAUNCHER_H);
            return 1;
        }
        if (event == '\n' || event == '\r') {
            if (matches > 0) {
                int index = launcher_match_at(launcher_selected);
                launcher_open = 0;
                launcher_reset_search();
                open_window(index);
            }
            return 1;
        }
        if (event == 8 || event == GUI_KEY_DELETE) {
            if (launcher_query_len > 0) {
                --launcher_query_len;
                launcher_query[launcher_query_len] = 0;
                launcher_selected = 0;
                gui_request_redraw_rect(18, TOPBAR_H + 8, LAUNCHER_W, LAUNCHER_H);
            }
            return 1;
        }
        if (!ctrl && event >= 32 && event < 127) {
            if (launcher_query_len < (int)sizeof(launcher_query) - 1) {
                launcher_query[launcher_query_len++] = (char)event;
                launcher_query[launcher_query_len] = 0;
                launcher_selected = 0;
                gui_request_redraw_rect(18, TOPBAR_H + 8, LAUNCHER_W, LAUNCHER_H);
            }
            return 1;
        }
        return 1;
    }

    if (quick_settings_open) {
        if (event == GUI_KEY_ESCAPE) {
            quick_settings_open = 0;
            gui_request_redraw_rect(0, TOPBAR_H, (int)framebuffer_info()->width,
                                    QUICK_H + 18);
            return 1;
        }
        if (event == '1' || event == '2' || event == '3') {
            if (event == '1') action_push(GUI_ACTION_THEME, 0);
            else if (event == '2') notifications_enabled = !notifications_enabled;
            else animations_enabled = !animations_enabled;
            if (!animations_enabled) finish_all_window_animations();
            gui_request_redraw();
            return 1;
        }
        return 1;
    }

    if (focused_window < 0 || focused_window >= WINDOW_COUNT) {
        if (event == GUI_KEY_ESCAPE) {
            launcher_open = 0;
            quick_settings_open = 0;
            power_open = 0;
            gui_request_redraw();
            return 1;
        }
        return 0;
    }

    if (ctrl && event >= '1' && event <= '5') return 0;
    if (focused_window == 0) return 0;

    if (focused_window == 3) {
        if (event == '\n' || event == '\r') {
            calculator_evaluate();
            return 1;
        }
        if (event == 8 || event == 127 || event == GUI_KEY_DELETE) {
            calculator_backspace();
            gui_request_redraw();
            return 1;
        }
        if (event >= 32 && event < 127) {
            char c = (char)event;
            if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '*' ||
                c == '/' || c == '%' || c == '(' || c == ')' || c == '.') {
                calculator_append_char(c);
                gui_request_redraw();
                return 1;
            }
        }
    }

    if (event == GUI_KEY_ESCAPE) return 1;
    if (event == GUI_KEY_CTRL_L) return 1;
    return 1;
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
    mouse_capture_window = -1;
    mouse_capture_mode = 0;
    last_title_click_window = -1;
    last_title_click_tick = 0;
    dock_hot = -1;
    for (int i = 0; i < 6; ++i) dock_hover_anim[i] = 0;
    launcher_open = 0;
    launcher_selected = 0;
    launcher_query_len = 0;
    launcher_query[0] = 0;
    for (int i = 0; i < 5; ++i) recent_apps[i] = i;
    quick_settings_open = 0;
    notification_open = 1;
    notifications_cleared = 0;
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

int gui_keyboard_event(int event, int ctrl, int shift) {
    return gui_keyboard_event_internal(event, ctrl, shift);
}

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
    draw_quick_settings();
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

static int resize_edges_at(const struct gui_window* w) {
    if (!w || w->maximized || w->minimized || w->closing || w->minimizing ||
        w->opening || w->w < MIN_WINDOW_W || w->h < MIN_WINDOW_H) return 0;
    int edges = 0;
    int right = w->x + w->w;
    int bottom = w->y + w->h;
    if (mouse_x_pos >= right - RESIZE_ZONE && mouse_x_pos <= right + 2) edges |= 1;
    if (mouse_y_pos >= bottom - RESIZE_ZONE && mouse_y_pos <= bottom + 2) edges |= 2;
    if (mouse_x_pos <= w->x + RESIZE_ZONE && mouse_x_pos >= w->x - 2) edges |= 4;
    if (mouse_y_pos <= w->y + RESIZE_ZONE && mouse_y_pos >= w->y - 2) edges |= 8;
    if (mouse_y_pos < w->y + TITLE_H) edges &= (uint8_t)~8u;
    return edges;
}

static void save_restore_geometry(struct gui_window* w) {
    if (!w->maximized && w->snap_state == 0) {
        w->restore_x = w->target_x;
        w->restore_y = w->target_y;
        w->restore_w = w->target_w;
        w->restore_h = w->target_h;
    }
}

static void toggle_maximize(struct gui_window* w) {
    if (!w) return;
    if (w->maximized || w->snap_state != 0) {
        window_set_target(w, w->restore_x, w->restore_y, w->restore_w, w->restore_h);
        w->maximized = 0;
        w->snap_state = 0;
        w->minimizing = 0;
        w->closing = 0;
        w->opening = 0;
        return;
    }
    save_restore_geometry(w);
    const framebuffer_info_t* f = framebuffer_info();
    if (!f) return;
    window_set_target(w, 4, TOPBAR_H + 2,
                      (int)f->width - 8, (int)f->height - TOPBAR_H - FOOTER_H - 6);
    w->maximized = 1;
    w->snap_state = 3;
}

static void apply_snap(struct gui_window* w) {
    if (!w || w->maximized || w->minimized || w->closing || w->minimizing) return;
    const framebuffer_info_t* f = framebuffer_info();
    if (!f) return;
    int sw = (int)f->width;
    int sh = (int)f->height;
    int work_h = sh - TOPBAR_H - FOOTER_H - 6;
    int half_w = (sw - 12) / 2;
    if (w->target_y <= TOPBAR_H + SNAP_ZONE) {
        toggle_maximize(w);
        return;
    }
    if (w->target_x <= SNAP_ZONE) {
        if (w->snap_state == 0) save_restore_geometry(w);
        window_set_target(w, 6, TOPBAR_H + 2, half_w, work_h);
        w->snap_state = 1;
    } else if (w->target_x + w->target_w >= sw - SNAP_ZONE) {
        if (w->snap_state == 0) save_restore_geometry(w);
        window_set_target(w, sw - half_w - 6, TOPBAR_H + 2, half_w, work_h);
        w->snap_state = 2;
    }
}

static void handle_title_action(int index, int control) {
    if (index < 0 || index >= WINDOW_COUNT) return;
    struct gui_window* w = &windows[index];
    int old_x = w->x, old_y = w->y, old_w = w->w, old_h = w->h;
    w->hover_control = 0;
    if (control == 1) {
        save_restore_geometry(w);
        int dx, dy, dw, dh;
        dock_geometry(index, &dx, &dy, &dw, &dh);
        w->target_x = dx;
        w->target_y = dy;
        w->target_w = dw;
        w->target_h = dh;
        w->minimizing = 1;
        w->maximized = 0;
        w->snap_state = 0;
        w->dragging = 0;
        w->resizing = 0;
        mouse_capture_window = -1;
        if (!animations_enabled) window_finish_transition(w);
        if (focused_window == index) focus_next_visible();
    } else if (control == 2) {
        toggle_maximize(w);
    } else if (control == 3) {
        int dx, dy, dw, dh;
        dock_geometry(index, &dx, &dy, &dw, &dh);
        w->target_x = dx;
        w->target_y = dy;
        w->target_w = dw;
        w->target_h = dh;
        w->closing = 1;
        w->dragging = 0;
        w->resizing = 0;
        mouse_capture_window = -1;
        if (!animations_enabled) window_finish_transition(w);
        if (focused_window == index) focus_next_visible();
    }
    window_transition_rect(w, old_x, old_y, old_w, old_h);
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

static void update_window_hover(void) {
    int old_index = -1;
    int old_control = 0;
    for (int i = 0; i < WINDOW_COUNT; ++i) {
        if (windows[i].hover_control) {
            old_index = i;
            old_control = windows[i].hover_control;
            break;
        }
        windows[i].hover_control = 0;
    }
    int new_index = -1;
    int new_control = 0;
    for (int oi = WINDOW_COUNT - 1; oi >= 0; --oi) {
        int idx = window_order[oi];
        struct gui_window* w = &windows[idx];
        if (!w->visible || w->minimized || w->closing) continue;
        if (inside_window(w, mouse_x_pos, mouse_y_pos)) {
            new_index = idx;
            new_control = title_control_at(w);
            break;
        }
    }
    for (int i = 0; i < WINDOW_COUNT; ++i) windows[i].hover_control = 0;
    if (new_index >= 0) windows[new_index].hover_control = new_control;
    if (old_index >= 0 && old_control != 0)
        gui_request_redraw_rect(windows[old_index].x + windows[old_index].w - 104,
                                windows[old_index].y + 4, 104, TITLE_H - 2);
    if (new_index >= 0 && new_control != 0)
        gui_request_redraw_rect(windows[new_index].x + windows[new_index].w - 104,
                                windows[new_index].y + 4, 104, TITLE_H - 2);
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
        gui_request_redraw_rect(((int)f->width - DOCK_W) / 2,
                                (int)f->height - DOCK_Y_FROM_BOTTOM, DOCK_W, DOCK_H);

    uint8_t pressed = (uint8_t)((new_buttons ^ mouse_prev_buttons) & new_buttons);
    uint8_t released = (uint8_t)((new_buttons ^ mouse_prev_buttons) & mouse_prev_buttons);

    if (pressed & 1u) {
        if (launcher_open) {
            int launcher_item = -1;
            if (point_in_launcher_item(&launcher_item)) {
                open_window(launcher_item);
                launcher_open = 0;
                launcher_reset_search();
                gui_request_redraw_rect(0, 0, LAUNCHER_W + 20, TOPBAR_H + LAUNCHER_H + 18);
            } else if (point_in_launcher_button()) {
                launcher_toggle();
            } else {
                launcher_open = 0;
                launcher_reset_search();
                gui_request_redraw_rect(0, 0, LAUNCHER_W + 20, TOPBAR_H + LAUNCHER_H + 18);
            }
        } else if (quick_settings_open) {
            if (point_in_quick_settings_item(0)) {
                action_push(GUI_ACTION_THEME, 0);
                gui_request_redraw();
            } else if (point_in_quick_settings_item(1)) {
                notifications_enabled = !notifications_enabled;
                gui_request_redraw();
            } else if (point_in_quick_settings_item(2)) {
                animations_enabled = !animations_enabled;
                if (!animations_enabled) finish_all_window_animations();
                gui_request_redraw();
            } else if (point_in_quick_settings_item(3)) {
                quick_settings_open = 0;
                open_window(4);
            } else if (point_in_launcher_button()) {
                launcher_toggle();
            } else {
                quick_settings_open = 0;
                gui_request_redraw_rect(0, TOPBAR_H, (int)f->width, QUICK_H + 18);
            }
        } else if (point_in_launcher_button()) {
            launcher_toggle();
        } else if (point_in_notification_clear()) {
            notifications_cleared = 1;
            notification_open = 1;
            gui_request_redraw();
        } else if (point_in_notification_bell()) {
            notification_open = !notification_open;
            power_open = 0;
            quick_settings_open = 0;
            gui_request_redraw();
        } else if (point_in_system_tray()) {
            quick_settings_open = 1;
            launcher_open = 0;
            power_open = 0;
            gui_request_redraw_rect((int)f->width - QUICK_W - 28, TOPBAR_H, QUICK_W + 20, QUICK_H + 20);
        } else if (point_in_clock_capsule()) {
            quick_settings_open = 0;
            open_window(4);
        } else if (point_in_power_button(0)) {
            action_push(GUI_ACTION_REBOOT, 0);
            power_open = 0;
            gui_request_redraw();
        } else if (point_in_power_button(1)) {
            action_push(GUI_ACTION_SHUTDOWN, 0);
            power_open = 0;
            gui_request_redraw();
        } else if (dock_hot >= 0) {
            if (dock_hot == 5) {
                power_open = !power_open;
                launcher_open = 0;
                quick_settings_open = 0;
                gui_request_redraw();
            } else {
                open_window(dock_hot);
            }
        } else if (mouse_capture_window < 0) {
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
                } else if (w->maximized == 0 && mouse_y_pos < w->y + TITLE_H) {
                    if (last_title_click_window == hit &&
                        (uint32_t)(runtime_ticks - last_title_click_tick) <= TITLE_DOUBLE_CLICK_TICKS) {
                        last_title_click_window = -1;
                        toggle_maximize(w);
                    } else {
                        last_title_click_window = hit;
                        last_title_click_tick = runtime_ticks;
                        w->dragging = 1;
                        mouse_capture_window = hit;
                        mouse_capture_mode = 1;
                        w->drag_dx = mouse_x_pos - w->x;
                        w->drag_dy = mouse_y_pos - w->y;
                    }
                } else {
                    int edges = resize_edges_at(w);
                    if (edges) {
                        w->resizing = 1;
                        w->resize_edges = edges;
                        w->resize_anchor_right = w->target_x + w->target_w;
                        w->resize_anchor_bottom = w->target_y + w->target_h;
                        w->drag_dx = mouse_x_pos - w->target_x;
                        w->drag_dy = mouse_y_pos - w->target_y;
                        mouse_capture_window = hit;
                        mouse_capture_mode = 2;
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
                            if (!animations_enabled) finish_all_window_animations();
                            gui_request_redraw_rect(w->x, w->y, w->w, w->h);
                        }
                    }
                }
                gui_request_redraw_rect(w->x - 12, w->y - 12, w->w + 24, w->h + 24);
            } else {
                focused_window = -1;
                power_open = 0;
                launcher_open = 0;
                gui_request_redraw();
            }
        }
    }

    if (mouse_capture_window >= 0 && mouse_capture_window < WINDOW_COUNT &&
        (new_buttons & 1u)) {
        struct gui_window* w = &windows[mouse_capture_window];
        if (mouse_capture_mode == 1 && w->dragging) {
            int old_x = w->x, old_y = w->y;
            w->target_x = mouse_x_pos - w->drag_dx;
            w->target_y = mouse_y_pos - w->drag_dy;
            clamp_target(w);
            w->maximized = 0;
            w->snap_state = 0;
            w->x = w->target_x;
            w->y = w->target_y;
            w->restore_x = w->x;
            w->restore_y = w->y;
            w->restore_w = w->w;
            w->restore_h = w->h;
            window_transition_rect(w, old_x, old_y, w->w, w->h);
        } else if (mouse_capture_mode == 2 && w->resizing) {
            int left = w->target_x;
            int top = w->target_y;
            int right = w->resize_anchor_right;
            int bottom = w->resize_anchor_bottom;
            if (w->resize_edges & 1) right = mouse_x_pos;
            if (w->resize_edges & 2) bottom = mouse_y_pos;
            if (w->resize_edges & 4) left = mouse_x_pos;
            if (w->resize_edges & 8) top = mouse_y_pos;
            if (right - left < MIN_WINDOW_W) {
                if (w->resize_edges & 4) left = right - MIN_WINDOW_W;
                else right = left + MIN_WINDOW_W;
            }
            if (bottom - top < MIN_WINDOW_H) {
                if (w->resize_edges & 8) top = bottom - MIN_WINDOW_H;
                else bottom = top + MIN_WINDOW_H;
            }
            int old_x = w->x, old_y = w->y, old_w = w->w, old_h = w->h;
            window_set_target(w, left, top, right - left, bottom - top);
            w->x = w->target_x;
            w->y = w->target_y;
            w->w = w->target_w;
            w->h = w->target_h;
            save_restore_geometry(w);
            window_transition_rect(w, old_x, old_y, old_w, old_h);
        }
    }

    if (released & 1u) {
        if (mouse_capture_window >= 0 && mouse_capture_window < WINDOW_COUNT) {
            struct gui_window* w = &windows[mouse_capture_window];
            if (w->dragging) apply_snap(w);
            w->dragging = 0;
            w->resizing = 0;
            w->resize_edges = 0;
        }
        mouse_capture_window = -1;
        mouse_capture_mode = 0;
    }

    if (wheel != 0 && mouse_capture_window < 0 &&
        inside_window(&windows[0], mouse_x_pos, mouse_y_pos)) {
        int delta = wheel > 0 ? -3 : 3;
        int max_top = max_int(0, TERM_ROWS - term_view_rows);
        term_view_top = max_int(0, min_int(term_view_top + delta, max_top));
        gui_request_terminal_redraw();
    }

    update_window_hover();
    mouse_prev_buttons = new_buttons;
}

void gui_set_runtime_ticks(uint32_t ticks) {
    if (!ready) return;
    if (ticks == runtime_ticks) return;
    runtime_ticks = ticks;
    if ((ticks % ANIMATION_TICKS) == 0u) advance_window_animations();
    if ((ticks % ANIMATION_TICKS) == 0u) {
        int dock_changed = 0;
        for (int i = 0; i < 6; ++i) {
            int target = i == dock_hot ? 100 : 0;
            int next = smooth_value(dock_hover_anim[i], target);
            if (next != dock_hover_anim[i]) {
                dock_hover_anim[i] = next;
                dock_changed = 1;
            }
        }
        if (dock_changed)
            gui_request_redraw_rect(((int)framebuffer_info()->width - DOCK_W) / 2 - 4,
                                    (int)framebuffer_info()->height - DOCK_Y_FROM_BOTTOM - 6,
                                    DOCK_W + 8, DOCK_H + 12);
    }
    if ((ticks % 25u) == 0u && windows[4].visible && !windows[4].minimized)
        gui_request_redraw_rect(windows[4].x, windows[4].y, windows[4].w, windows[4].h);
    if ((ticks % 100u) == 0u)
        gui_request_redraw_rect(0, 0, (int)framebuffer_info()->width, TOPBAR_H);
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
