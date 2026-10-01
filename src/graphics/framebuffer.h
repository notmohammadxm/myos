#ifndef MYOS_FRAMEBUFFER_H
#define MYOS_FRAMEBUFFER_H

#include <stdint.h>

typedef struct {
    volatile uint8_t* address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
    uint8_t type;
    uint8_t red_pos;
    uint8_t red_mask;
    uint8_t green_pos;
    uint8_t green_mask;
    uint8_t blue_pos;
    uint8_t blue_mask;
    uint8_t present;
} framebuffer_info_t;

void framebuffer_set_info(uint64_t address, uint32_t pitch, uint32_t width,
                          uint32_t height, uint8_t bpp, uint8_t type,
                          uint8_t red_pos, uint8_t red_mask,
                          uint8_t green_pos, uint8_t green_mask,
                          uint8_t blue_pos, uint8_t blue_mask);

const framebuffer_info_t* framebuffer_info(void);
int framebuffer_available(void);
uint32_t framebuffer_make_color(uint8_t r, uint8_t g, uint8_t b);
void framebuffer_clear(uint32_t color);
void framebuffer_pixel(int x, int y, uint32_t color);
void framebuffer_fill_rect(int x, int y, int width, int height, uint32_t color);

#endif
