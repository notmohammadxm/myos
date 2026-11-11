#ifndef MYOS_MOUSE_H
#define MYOS_MOUSE_H

#include <stdint.h>

typedef struct {
    int32_t dx;
    int32_t dy;
    int32_t wheel;
    uint8_t buttons;
} mouse_event_t;

int mouse_init(void);
void mouse_set_bounds(uint32_t width, uint32_t height);
void mouse_set_sensitivity(uint8_t percent);
uint8_t mouse_get_sensitivity(void);
int mouse_available(void);
#ifdef UNIT_TEST
void mouse_test_reset(void);
void mouse_test_feed_wheel_packet(uint8_t flags, uint8_t dx, uint8_t dy, uint8_t wheel);
#endif
int mouse_poll(mouse_event_t* event);
int32_t mouse_x(void);
int32_t mouse_y(void);
uint8_t mouse_buttons(void);

void mouse_irq_handler(void);

#endif
