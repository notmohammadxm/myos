#ifndef MYOS_FONT_H
#define MYOS_FONT_H
#include <stdint.h>
const uint8_t* font8x14_get(char c);
#define FONT8X14_GLYPH_W 8
#define FONT8X14_GLYPH_H 14
#define FONT8X14_W 9
#define FONT8X14_H 16
#endif
