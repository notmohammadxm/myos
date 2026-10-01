#include "gui.h"
#include "../graphics/framebuffer.h"
#include "../graphics/renderer.h"
#include <stddef.h>

#define FONT_W 6
#define FONT_H 8
#define TERM_COLS 96
#define TERM_ROWS 48
#define TERM_X 42
#define TERM_Y 132
#define TERM_PAD 12

struct gui_window {
    int x, y, w, h;
    int dragging;
    int drag_dx, drag_dy;
};

static int ready;
static const char* theme_name = "matrix";
static uint32_t bg, panel, panel2, accent, text, muted, border, terminal_bg;
static char term[TERM_ROWS][TERM_COLS];
static uint8_t term_attr[TERM_ROWS][TERM_COLS];
static int tx, ty;
static int input_x, input_y, input_rendered_len;
static int input_active;
static int cursor_visible = 1;
static int mouse_x_pos = 512;
static int mouse_y_pos = 384;
static uint8_t mouse_prev_buttons;
static int focused_window;
static struct gui_window windows[3] = {
    { 24, 104, 650, 480, 0, 0, 0 },
    { 700, 92, 292, 240, 0, 0, 0 },
    { 700, 356, 292, 228, 0, 0, 0 }
};

static uint8_t glyph_row(char c, int row) {
    static const uint8_t digits[10][7] = {
        {30,33,35,45,49,33,30},{12,28,12,12,12,12,30},
        {30,33,1,2,4,8,63},{30,33,1,14,1,33,30},
        {2,6,10,18,34,63,2},{63,32,60,2,1,33,30},
        {14,16,32,62,33,33,30},{63,33,2,4,8,8,8},
        {30,33,33,30,33,33,30},{30,33,33,31,1,2,28}
    };
    static const uint8_t upper[26][7] = {
        {30,33,33,63,33,33,33},{62,33,33,62,33,33,62},
        {30,33,32,32,32,33,30},{60,34,33,33,33,34,60},
        {63,32,32,62,32,32,63},{63,32,32,62,32,32,32},
        {30,33,32,47,33,33,31},{33,33,33,63,33,33,33},
        {30,12,12,12,12,12,30},{15,4,4,4,4,36,24},
        {33,34,36,56,36,34,33},{32,32,32,32,32,32,63},
        {33,51,45,45,33,33,33},{33,49,41,37,35,33,33},
        {30,33,33,33,33,33,30},{62,33,33,62,32,32,32},
        {30,33,33,33,37,34,29},{62,33,33,62,36,34,33},
        {31,32,32,30,1,1,62},{63,12,12,12,12,12,12},
        {33,33,33,33,33,33,30},{33,33,33,33,33,18,12},
        {33,33,33,45,45,51,33},{33,33,18,12,18,33,33},
        {33,33,18,12,12,12,12},{63,2,4,8,16,32,63}
    };
    static const uint8_t punct[16][7] = {
        {0,0,0,0,0,0,0}, {0,0,12,12,12,0,12}, {54,54,20,0,0,0,0},
        {10,31,10,10,31,10,0}, {4,30,32,28,2,60,4}, {0,35,6,12,24,49,0},
        {28,34,36,24,37,34,29}, {0,6,6,4,0,0,0}, {2,4,8,8,8,4,2},
        {16,8,4,4,4,8,16}, {0,4,21,14,21,4,0}, {0,4,4,31,4,4,0},
        {0,0,0,0,6,6,4}, {0,0,31,0,0,0,0}, {0,0,0,0,12,12,0},
        {1,2,4,8,16,32,0}
    };
    if (row < 0 || row >= 7) return 0;
    if (c >= '0' && c <= '9') return digits[c - '0'][row];
    if (c >= 'A' && c <= 'Z') return upper[c - 'A'][row];
    if (c >= 'a' && c <= 'z') return upper[c - 'a'][row];
    switch (c) {
        case ' ': return 0;
        case '!': return punct[1][row];
        case '"': return punct[2][row];
        case '#': return punct[3][row];
        case '$': return punct[4][row];
        case '%': return punct[5][row];
        case '&': return punct[6][row];
        case '\'': return punct[7][row];
        case '(': return punct[8][row];
        case ')': return punct[9][row];
        case '+': return punct[10][row];
        case ',': return punct[11][row];
        case '-': return punct[12][row];
        case '.': return punct[13][row];
        case '/': return punct[15][row];
        case ':': return punct[2][row];
        case ';': return punct[11][row];
        case '=': return punct[12][row];
        case '?': return 30; /* fallback visual */
        case '_': return row == 6 ? 63 : 0;
        case '|': return row == 3 ? 4 : 4;
        default: return 0;
    }
}

static void draw_char(int x, int y, char c, uint32_t color) {
    for (int r = 0; r < 7; ++r) {
        uint8_t bits = glyph_row(c, r);
        for (int col = 0; col < 6; ++col)
            if (bits & (1u << (5 - col))) framebuffer_pixel(x + col, y + r, color);
    }
}

static void draw_text(int x, int y, const char* s, uint32_t color) {
    if (!s) return;
    int px = x;
    while (*s) {
        draw_char(px, y, *s++, color);
        px += FONT_W;
    }
}

static int inside(const struct gui_window* w, int x, int y) {
    return x >= w->x && y >= w->y && x < w->x + w->w && y < w->y + w->h;
}

static void set_palette(const char* name) {
    theme_name = name ? name : "matrix";
    if (theme_name[0] == 'i' && theme_name[1] == 'c') {
        bg = framebuffer_make_color(8,17,27); panel = framebuffer_make_color(10,22,36);
        panel2 = framebuffer_make_color(15,31,49); accent = framebuffer_make_color(112,214,255);
        text = framebuffer_make_color(224,245,255); muted = framebuffer_make_color(130,168,188);
        border = framebuffer_make_color(52,120,150); terminal_bg = framebuffer_make_color(4,13,22);
    } else if (theme_name[0] == 'a') {
        bg = framebuffer_make_color(25,18,8); panel = framebuffer_make_color(34,23,10);
        panel2 = framebuffer_make_color(45,29,11); accent = framebuffer_make_color(255,209,102);
        text = framebuffer_make_color(255,243,207); muted = framebuffer_make_color(185,167,126);
        border = framebuffer_make_color(130,98,35); terminal_bg = framebuffer_make_color(13,9,4);
    } else if (theme_name[0] == 'm' && theme_name[1] == 'o') {
        bg = framebuffer_make_color(13,13,13); panel = framebuffer_make_color(23,23,23);
        panel2 = framebuffer_make_color(29,29,29); accent = framebuffer_make_color(241,241,241);
        text = framebuffer_make_color(242,242,242); muted = framebuffer_make_color(153,153,153);
        border = framebuffer_make_color(85,85,85); terminal_bg = framebuffer_make_color(7,7,7);
    } else {
        bg = framebuffer_make_color(4,14,8); panel = framebuffer_make_color(10,25,16);
        panel2 = framebuffer_make_color(13,31,20); accent = framebuffer_make_color(57,255,136);
        text = framebuffer_make_color(216,255,230); muted = framebuffer_make_color(129,165,142);
        border = framebuffer_make_color(40,110,65); terminal_bg = framebuffer_make_color(2,9,4);
    }
}

static void draw_window(const struct gui_window* w, const char* title, int index) {
    uint32_t line = index == focused_window ? accent : border;
    renderer_rect(w->x, w->y, w->w, w->h, panel);
    renderer_border(w->x, w->y, w->w, w->h, 1, line);
    renderer_rect(w->x, w->y, w->w, 34, panel2);
    draw_text(w->x + 12, w->y + 12, title, text);
    framebuffer_fill_rect(w->x + w->w - 54, w->y + 10, 5, 5, muted);
    framebuffer_fill_rect(w->x + w->w - 41, w->y + 10, 5, 5, muted);
    framebuffer_fill_rect(w->x + w->w - 28, w->y + 10, 5, 5, accent);
}

static void draw_sysinfo(const struct gui_window* w) {
    draw_text(w->x + 14, w->y + 54, "KERNEL", muted);
    draw_text(w->x + 14, w->y + 69, "I386", text);
    draw_text(w->x + 150, w->y + 54, "BOOT", muted);
    draw_text(w->x + 150, w->y + 69, "MULTIBOOT2", text);
    draw_text(w->x + 14, w->y + 108, "CPU", muted);
    draw_text(w->x + 14, w->y + 123, "READY", accent);
    draw_text(w->x + 150, w->y + 108, "NIC", muted);
    draw_text(w->x + 150, w->y + 123, "DETECTED", accent);
    draw_text(w->x + 14, w->y + 164, "MEMORY", muted);
    renderer_rect(w->x + 14, w->y + 182, 260, 8, bg);
    renderer_rect(w->x + 14, w->y + 182, 74, 8, accent);
    draw_text(w->x + 14, w->y + 202, "RAM  28%", text);
}

static void draw_settings(const struct gui_window* w) {
    draw_text(w->x + 14, w->y + 54, "THEME", muted);
    draw_text(w->x + 14, w->y + 69, theme_name, text);
    renderer_border(w->x + 196, w->y + 51, 76, 24, 1, border);
    draw_text(w->x + 208, w->y + 60, ">", accent);
    draw_text(w->x + 14, w->y + 112, "NOTIFICATIONS", muted);
    renderer_rect(w->x + 214, w->y + 106, 48, 18, accent);
    renderer_rect(w->x + 237, w->y + 109, 12, 12, bg);
    draw_text(w->x + 14, w->y + 156, "ANIMATIONS", muted);
    renderer_rect(w->x + 214, w->y + 150, 48, 18, border);
    renderer_rect(w->x + 217, w->y + 153, 12, 12, muted);
    draw_text(w->x + 14, w->y + 200, "SESSION ONLY", accent);
}

static void draw_cursor(void) {
    static const int shape[12][2] = {
        {0,0},{0,1},{0,2},{0,3},{0,4},{0,5},{0,6},{0,7},{0,8},{1,7},{2,10},{3,11}
    };
    for (int i = 0; i < 12; ++i) {
        framebuffer_pixel(mouse_x_pos + shape[i][0], mouse_y_pos + shape[i][1], text);
        if (i > 1) framebuffer_pixel(mouse_x_pos + shape[i][0] + 1, mouse_y_pos + shape[i][1], text);
    }
}

static void layout_windows(void) {
    const framebuffer_info_t* f = framebuffer_info();
    int screen_w = (int)f->width;
    int screen_h = (int)f->height;
    int side_x = screen_w - 324;
    if (side_x < 690) side_x = 690;
    windows[0].x = 24;
    windows[0].y = 104;
    windows[0].w = side_x - 50;
    if (windows[0].w < 520) windows[0].w = 520;
    windows[0].h = screen_h - 188;
    if (windows[0].h < 390) windows[0].h = 390;

    windows[1].x = windows[0].x + windows[0].w + 26;
    windows[1].y = 104;
    windows[1].w = screen_w - windows[1].x - 24;
    windows[1].h = 240;
    windows[2].x = windows[1].x;
    windows[2].y = windows[1].y + windows[1].h + 24;
    windows[2].w = windows[1].w;
    windows[2].h = 228;

    if (windows[1].w < 250) windows[1].w = 250;
    if (windows[2].w < 250) windows[2].w = 250;
}

static void draw_desktop_chrome(void) {
    const framebuffer_info_t* f = framebuffer_info();
    int w = (int)f->width, h = (int)f->height;
    renderer_rect(0, 0, w, h, bg);
    renderer_rect(0, 0, w, 50, panel2);
    renderer_rect(0, h - 66, w, 66, panel2);
    renderer_border(0, 0, w, h, 1, border);
    draw_text(18, 17, "MYOS", accent);
    draw_text(75, 17, "TERMINAL DESKTOP", muted);
    draw_text(w - 180, 17, "CORE ONLINE", accent);
    draw_text(w - 92, 17, "10:42", text);

    renderer_rect(w/2 - 150, h - 54, 300, 42, panel);
    renderer_border(w/2 - 150, h - 54, 300, 42, 1, border);
    draw_text(w/2 - 130, h - 40, "TERM", accent);
    draw_text(w/2 - 62, h - 40, "SYSTEM", text);
    draw_text(w/2 + 17, h - 40, "SETTINGS", text);
    draw_text(w/2 + 104, h - 40, "POWER", muted);
    draw_text(18, h - 40, "MYOS CORE", text);
    draw_text(w - 138, h - 40, "SESSION ONLY", muted);
}

static void draw_terminal_surface(const struct gui_window* w) {
    renderer_rect(w->x + 1, w->y + 35, w->w - 2, w->h - 36, terminal_bg);
    int cols = (w->w - TERM_PAD * 2) / FONT_W;
    int rows = (w->h - 35 - TERM_PAD * 2) / FONT_H;
    if (cols > TERM_COLS) cols = TERM_COLS;
    if (rows > TERM_ROWS) rows = TERM_ROWS;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            char ch = term[r][c];
            if (ch) draw_char(w->x + TERM_PAD + c * FONT_W,
                             w->y + 45 + r * FONT_H, ch,
                             term_attr[r][c] == 0x0C ? framebuffer_make_color(255,100,120) :
                             term_attr[r][c] == 0x0E ? framebuffer_make_color(255,210,100) :
                             term_attr[r][c] == 0x0B ? accent : text);
        }
    }
    if (cursor_visible && focused_window == 0) {
        int cx = w->x + TERM_PAD + tx * FONT_W;
        int cy = w->y + 45 + ty * FONT_H;
        renderer_rect(cx, cy + 6, FONT_W - 1, 2, accent);
    }
}

void gui_init(void) {
    ready = renderer_available();
    if (!ready) return;
    set_palette("matrix");
    layout_windows();
    for (int r = 0; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c) { term[r][c] = ' '; term_attr[r][c] = 0x0A; }
    tx = ty = 0;
    input_x = input_y = input_rendered_len = 0;
    input_active = 0;
    mouse_x_pos = (int)framebuffer_info()->width / 2;
    mouse_y_pos = (int)framebuffer_info()->height / 2;
    gui_redraw();
}

int gui_available(void) { return ready != 0; }

void gui_set_theme(const char* name) {
    if (!ready) return;
    set_palette(name);
    gui_redraw();
}

void gui_terminal_clear(void) {
    if (!ready) return;
    for (int r = 0; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c) { term[r][c] = ' '; term_attr[r][c] = 0x0A; }
    tx = ty = 0;
    input_x = input_y = input_rendered_len = 0;
    input_active = 0;
    gui_redraw();
}

void gui_terminal_begin_input(void) {
    if (!ready) return;
    input_x = tx;
    input_y = ty;
    input_rendered_len = 0;
    input_active = 1;
}

void gui_terminal_edit(const char* text, int len, int cursor_pos, uint8_t shell_color) {
    if (!ready || !input_active || !text) return;
    if (len < 0) len = 0;
    if (len >= TERM_COLS) len = TERM_COLS - 1;
    if (cursor_pos < 0) cursor_pos = 0;
    if (cursor_pos > len) cursor_pos = len;
    for (int i = 0; i < input_rendered_len; ++i) {
        int x = input_x + i;
        if (x >= TERM_COLS) break;
        term[input_y][x] = ' ';
        term_attr[input_y][x] = 0x0A;
    }
    for (int i = 0; i < len; ++i) {
        int x = input_x + i;
        if (x >= TERM_COLS) break;
        term[input_y][x] = text[i];
        term_attr[input_y][x] = shell_color;
    }
    input_rendered_len = len;
    tx = input_x + len;
    ty = input_y;
    gui_redraw();
    tx = input_x + cursor_pos;
    ty = input_y;
    gui_redraw();
}

void gui_terminal_set_cursor(int cursor_pos) {
    if (!ready || !input_active) return;
    if (cursor_pos < 0) cursor_pos = 0;
    if (cursor_pos > input_rendered_len) cursor_pos = input_rendered_len;
    tx = input_x + cursor_pos;
    ty = input_y;
    gui_redraw();
}

static void terminal_scroll(void) {
    for (int r = 1; r < TERM_ROWS; ++r)
        for (int c = 0; c < TERM_COLS; ++c) { term[r-1][c] = term[r][c]; term_attr[r-1][c] = term_attr[r][c]; }
    for (int c = 0; c < TERM_COLS; ++c) { term[TERM_ROWS-1][c] = ' '; term_attr[TERM_ROWS-1][c] = 0x0A; }
    ty = TERM_ROWS - 1;
}

void gui_terminal_putchar(char c, uint8_t shell_color) {
    if (!ready) return;
    if (c == '\r') { tx = 0; gui_redraw(); return; }
    if (c == '\b') { if (tx > 0) --tx; else if (ty > 0) { --ty; tx = TERM_COLS - 1; } term[ty][tx] = ' '; gui_redraw(); return; }
    if (c == '\t') { int n = 4 - (tx % 4); while (n--) gui_terminal_putchar(' ', shell_color); return; }
    if (c == '\n') { tx = 0; ++ty; if (ty >= TERM_ROWS) terminal_scroll(); gui_redraw(); return; }
    if ((unsigned char)c < 32u) return;
    if (tx >= TERM_COLS) { tx = 0; ++ty; if (ty >= TERM_ROWS) terminal_scroll(); }
    term[ty][tx] = c;
    term_attr[ty][tx] = shell_color;
    ++tx;
    if (tx >= TERM_COLS) { tx = 0; ++ty; if (ty >= TERM_ROWS) terminal_scroll(); }
    gui_redraw();
}

void gui_redraw(void) {
    if (!ready) return;
    const framebuffer_info_t* f = framebuffer_info();
    draw_desktop_chrome();
    draw_window(&windows[0], "TERMINAL", 0);
    draw_window(&windows[1], "SYSTEM INFO", 1);
    draw_window(&windows[2], "SETTINGS", 2);
    draw_terminal_surface(&windows[0]);
    draw_sysinfo(&windows[1]);
    draw_settings(&windows[2]);
    int nw = 212;
    int nx = (int)f->width - nw - 26;
    int ny = (int)f->height - 132;
    renderer_rect(nx, ny, nw, 50, panel2);
    renderer_border(nx, ny, nw, 50, 1, border);
    draw_text(nx + 14, ny + 11, "SYSTEM NOTIFICATION", accent);
    draw_text(nx + 14, ny + 26, "MYOS BOOT COMPLETED", text);
    draw_cursor();
}

void gui_mouse_event(int dx, int dy, int wheel, uint8_t new_buttons) {
    (void)wheel;
    if (!ready) return;
    const framebuffer_info_t* f = framebuffer_info();
    mouse_x_pos += dx;
    mouse_y_pos += dy;
    if (mouse_x_pos < 0) mouse_x_pos = 0;
    if (mouse_y_pos < 50) mouse_y_pos = 50;
    if (mouse_x_pos >= (int)f->width) mouse_x_pos = (int)f->width - 1;
    if (mouse_y_pos >= (int)f->height - 67) mouse_y_pos = (int)f->height - 68;

    uint8_t pressed = (uint8_t)((new_buttons ^ mouse_prev_buttons) & new_buttons);
    if (pressed & 1u) {
        focused_window = -1;
        for (int i = 2; i >= 0; --i) {
            if (inside(&windows[i], mouse_x_pos, mouse_y_pos)) { focused_window = i; break; }
        }
        if (focused_window >= 0) {
            struct gui_window* w = &windows[focused_window];
            if (mouse_y_pos < w->y + 34) {
                w->dragging = 1;
                w->drag_dx = mouse_x_pos - w->x;
                w->drag_dy = mouse_y_pos - w->y;
            }
        }
    }
    if (!(new_buttons & 1u)) {
        for (int i = 0; i < 3; ++i) windows[i].dragging = 0;
    } else if (focused_window >= 0 && windows[focused_window].dragging) {
        struct gui_window* w = &windows[focused_window];
        w->x = mouse_x_pos - w->drag_dx;
        w->y = mouse_y_pos - w->drag_dy;
        if (w->x < 4) w->x = 4;
        if (w->y < 54) w->y = 54;
    }
    mouse_prev_buttons = new_buttons;
    gui_redraw();
}
