#include "renderer.h"
#include "framebuffer.h"
#include <stddef.h>

#define RENDER_MAX_WIDTH  1280u
#define RENDER_MAX_HEIGHT 800u

/* One 32-bit packed-pixel backbuffer. The requested boot mode is 1024x768x32,
 * so this remains a bounded, static allocation and requires no heap. */
static uint32_t backbuffer[RENDER_MAX_WIDTH * RENDER_MAX_HEIGHT];
static int ready;
static uint32_t buffer_width;
static uint32_t buffer_height;
static uint32_t buffer_pitch_pixels;
static int dirty;
static int dirty_x0, dirty_y0, dirty_x1, dirty_y1;

static void mark_dirty_rect(int x, int y, int width, int height) {
    if (!ready || width <= 0 || height <= 0) return;
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > (int)buffer_width) width = (int)buffer_width - x;
    if (y + height > (int)buffer_height) height = (int)buffer_height - y;
    if (width <= 0 || height <= 0) return;
    int x1 = x + width, y1 = y + height;
    if (!dirty) { dirty_x0=x; dirty_y0=y; dirty_x1=x1; dirty_y1=y1; dirty=1; return; }
    if (x < dirty_x0) dirty_x0=x;
    if (y < dirty_y0) dirty_y0=y;
    if (x1 > dirty_x1) dirty_x1=x1;
    if (y1 > dirty_y1) dirty_y1=y1;
}

void renderer_reset_dirty(void) {
    dirty = 0;
    dirty_x0 = dirty_y0 = dirty_x1 = dirty_y1 = 0;
}

void renderer_mark_dirty_rect(int x, int y, int width, int height) {
    if (!ready) return;
    mark_dirty_rect(x, y, width, height);
}

static int clip_rect(int* x, int* y, int* width, int* height) {
    if (!x || !y || !width || !height || *width <= 0 || *height <= 0) return 0;
    int x0 = *x;
    int y0 = *y;
    int x1 = x0 + *width;
    int y1 = y0 + *height;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)buffer_width) x1 = (int)buffer_width;
    if (y1 > (int)buffer_height) y1 = (int)buffer_height;
    if (x0 >= x1 || y0 >= y1) return 0;
    *x = x0;
    *y = y0;
    *width = x1 - x0;
    *height = y1 - y0;
    return 1;
}

void renderer_init(void) {
    const framebuffer_info_t* f = framebuffer_info();
    ready = 0;
    dirty = 0;
    dirty_x0 = dirty_y0 = dirty_x1 = dirty_y1 = 0;
    buffer_width = buffer_height = buffer_pitch_pixels = 0;
    if (!framebuffer_available() || !f) return;
    if (f->width == 0 || f->height == 0) return;
    if (f->width > RENDER_MAX_WIDTH || f->height > RENDER_MAX_HEIGHT) return;
    buffer_width = f->width;
    buffer_height = f->height;
    buffer_pitch_pixels = RENDER_MAX_WIDTH;
    for (uint32_t y = 0; y < buffer_height; ++y) {
        for (uint32_t x = 0; x < buffer_width; ++x)
            backbuffer[y * buffer_pitch_pixels + x] = 0;
    }
    ready = 1;
}

int renderer_available(void) {
    return ready != 0;
}

void renderer_pixel(int x, int y, uint32_t color) {
    if (!ready) return;
    if (x < 0 || y < 0 || (uint32_t)x >= buffer_width || (uint32_t)y >= buffer_height) return;
    backbuffer[(uint32_t)y * buffer_pitch_pixels + (uint32_t)x] = color;
    mark_dirty_rect(x, y, 1, 1);
}

void renderer_clear(uint32_t color) {
    if (!ready) return;
    for (uint32_t y = 0; y < buffer_height; ++y) {
        uint32_t* row = &backbuffer[y * buffer_pitch_pixels];
        for (uint32_t x = 0; x < buffer_width; ++x) row[x] = color;
    }
    mark_dirty_rect(0, 0, (int)buffer_width, (int)buffer_height);
}

void renderer_rect(int x, int y, int width, int height, uint32_t color) {
    if (!ready) return;
    if (!clip_rect(&x, &y, &width, &height)) return;
    for (int py = y; py < y + height; ++py) {
        uint32_t* row = &backbuffer[(uint32_t)py * buffer_pitch_pixels + (uint32_t)x];
        for (int px = 0; px < width; ++px) row[px] = color;
    }
    mark_dirty_rect(x, y, width, height);
}

void renderer_border(int x, int y, int width, int height, int thickness, uint32_t color) {
    if (!ready || thickness <= 0 || width <= 0 || height <= 0) return;
    if (thickness * 2 > width) thickness = width / 2;
    if (thickness * 2 > height) thickness = height / 2;
    renderer_rect(x, y, width, thickness, color);
    renderer_rect(x, y + height - thickness, width, thickness, color);
    renderer_rect(x, y + thickness, thickness, height - thickness * 2, color);
    renderer_rect(x + width - thickness, y + thickness, thickness,
                  height - thickness * 2, color);
}

void renderer_line(int x0, int y0, int x1, int y1, uint32_t color) {
    if (!ready) return;
    int ox0 = x0, oy0 = y0, ox1 = x1, oy1 = y1;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? -(y1 - y0) : -(y0 - y1);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && y0 >= 0 && (uint32_t)x0 < buffer_width &&
            (uint32_t)y0 < buffer_height) {
            backbuffer[(uint32_t)y0 * buffer_pitch_pixels + (uint32_t)x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    int minx = ox0 < ox1 ? ox0 : ox1; int maxx = ox0 > ox1 ? ox0 : ox1;
    int miny = oy0 < oy1 ? oy0 : oy1; int maxy = oy0 > oy1 ? oy0 : oy1;
    mark_dirty_rect(minx, miny, maxx-minx+1, maxy-miny+1);
}

void renderer_present_rect(int x, int y, int width, int height) {
    if (!ready) return;
    const framebuffer_info_t* f = framebuffer_info();
    if (!f || !f->address || f->bpp != 32u) return;
    if (!clip_rect(&x, &y, &width, &height)) return;

    for (int py = y; py < y + height; ++py) {
        const uint8_t* src = (const uint8_t*)&backbuffer[(uint32_t)py * buffer_pitch_pixels + (uint32_t)x];
        volatile uint8_t* dst = f->address + (uint32_t)py * f->pitch + (uint32_t)x * 4u;
        for (int px = 0; px < width * 4; ++px) dst[px] = src[px];
    }
}

void renderer_present(void) {
    if (!ready || !dirty) return;
    renderer_present_rect(dirty_x0, dirty_y0, dirty_x1-dirty_x0, dirty_y1-dirty_y0);
    renderer_reset_dirty();
}
