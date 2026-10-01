#include "framebuffer.h"
#include <stddef.h>

static framebuffer_info_t fb;

static int mask_ok(uint8_t pos, uint8_t size) {
    if (size == 0u || size > 8u) return 0;
    if (pos >= 32u) return 0;
    if ((uint32_t)pos + (uint32_t)size > 32u) return 0;
    return 1;
}

void framebuffer_set_info(uint64_t address, uint32_t pitch, uint32_t width,
                          uint32_t height, uint8_t bpp, uint8_t type,
                          uint8_t red_pos, uint8_t red_mask,
                          uint8_t green_pos, uint8_t green_mask,
                          uint8_t blue_pos, uint8_t blue_mask) {
    fb.present = 0;
    fb.address = (volatile uint8_t*)0;
    fb.width = fb.height = fb.pitch = 0;
    fb.bpp = fb.type = 0;
    fb.red_pos = fb.red_mask = 0;
    fb.green_pos = fb.green_mask = 0;
    fb.blue_pos = fb.blue_mask = 0;

    if (!address || (address >> 32) != 0u) return;
    if (!width || !height || !pitch) return;
    if (width < 1024u || height < 600u) return;
    if (width > 8192u || height > 8192u) return;
    if (bpp != 32u) return;
    if (type != 1u) return; /* direct RGB */
    if (!mask_ok(red_pos, red_mask) || !mask_ok(green_pos, green_mask) ||
        !mask_ok(blue_pos, blue_mask)) return;
    if (pitch < width * 4u) return;
    if ((uint64_t)pitch * (uint64_t)height > 0xFFFFFFFFu) return;

    fb.address = (volatile uint8_t*)(uintptr_t)(uint32_t)address;
    fb.width = width;
    fb.height = height;
    fb.pitch = pitch;
    fb.bpp = bpp;
    fb.type = type;
    fb.red_pos = red_pos;
    fb.red_mask = red_mask;
    fb.green_pos = green_pos;
    fb.green_mask = green_mask;
    fb.blue_pos = blue_pos;
    fb.blue_mask = blue_mask;
    fb.present = 1;
}

const framebuffer_info_t* framebuffer_info(void) {
    return &fb;
}

int framebuffer_available(void) {
    return fb.present != 0;
}

static uint32_t scale_channel(uint8_t value, uint8_t bits) {
    if (bits >= 8u) return value;
    return ((uint32_t)value * ((1u << bits) - 1u) + 127u) / 255u;
}

uint32_t framebuffer_make_color(uint8_t r, uint8_t g, uint8_t b) {
    if (!framebuffer_available()) return 0;
    uint32_t out = 0;
    out |= (scale_channel(r, fb.red_mask) & ((1u << fb.red_mask) - 1u)) << fb.red_pos;
    out |= (scale_channel(g, fb.green_mask) & ((1u << fb.green_mask) - 1u)) << fb.green_pos;
    out |= (scale_channel(b, fb.blue_mask) & ((1u << fb.blue_mask) - 1u)) << fb.blue_pos;
    return out;
}

void framebuffer_pixel(int x, int y, uint32_t color) {
    if (!framebuffer_available()) return;
    if (x < 0 || y < 0 || (uint32_t)x >= fb.width || (uint32_t)y >= fb.height) return;
    volatile uint32_t* row = (volatile uint32_t*)(fb.address + (uint32_t)y * fb.pitch);
    row[x] = color;
}

void framebuffer_fill_rect(int x, int y, int width, int height, uint32_t color) {
    if (!framebuffer_available() || width <= 0 || height <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + width;
    int y1 = y + height;
    if (x1 > (int)fb.width) x1 = (int)fb.width;
    if (y1 > (int)fb.height) y1 = (int)fb.height;
    if (x0 >= x1 || y0 >= y1) return;

    for (int py = y0; py < y1; ++py) {
        volatile uint32_t* row = (volatile uint32_t*)(fb.address + (uint32_t)py * fb.pitch);
        for (int px = x0; px < x1; ++px) row[px] = color;
    }
}

void framebuffer_clear(uint32_t color) {
    if (!framebuffer_available()) return;
    framebuffer_fill_rect(0, 0, (int)fb.width, (int)fb.height, color);
}
