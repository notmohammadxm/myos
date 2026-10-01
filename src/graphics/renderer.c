#include "renderer.h"
#include "framebuffer.h"
#include <stddef.h>

static int ready;

void renderer_init(void) {
    ready = framebuffer_available();
}

int renderer_available(void) {
    return ready;
}

void renderer_clear(uint32_t color) {
    if (!ready) return;
    framebuffer_clear(color);
}

void renderer_rect(int x, int y, int width, int height, uint32_t color) {
    if (!ready) return;
    framebuffer_fill_rect(x, y, width, height, color);
}

void renderer_border(int x, int y, int width, int height, int thickness, uint32_t color) {
    if (!ready || thickness <= 0) return;
    if (width <= 0 || height <= 0) return;
    if (thickness * 2 > width) thickness = width / 2;
    if (thickness * 2 > height) thickness = height / 2;
    framebuffer_fill_rect(x, y, width, thickness, color);
    framebuffer_fill_rect(x, y + height - thickness, width, thickness, color);
    framebuffer_fill_rect(x, y + thickness, thickness, height - thickness * 2, color);
    framebuffer_fill_rect(x + width - thickness, y + thickness, thickness,
                          height - thickness * 2, color);
}

void renderer_line(int x0, int y0, int x1, int y1, uint32_t color) {
    if (!ready) return;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? -(y1 - y0) : -(y0 - y1);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        framebuffer_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
