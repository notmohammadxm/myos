#ifndef MYOS_RENDERER_H
#define MYOS_RENDERER_H

#include <stdint.h>

void renderer_init(void);
int renderer_available(void);
void renderer_clear(uint32_t color);
void renderer_rect(int x, int y, int width, int height, uint32_t color);
void renderer_border(int x, int y, int width, int height, int thickness, uint32_t color);
void renderer_pixel(int x, int y, uint32_t color);
void renderer_line(int x0, int y0, int x1, int y1, uint32_t color);
void renderer_present(void);
void renderer_present_rect(int x, int y, int width, int height);
void renderer_reset_dirty(void);
void renderer_mark_dirty_rect(int x, int y, int width, int height);
void renderer_set_clip_rect(int x, int y, int width, int height);
void renderer_clear_clip(void);

#endif
