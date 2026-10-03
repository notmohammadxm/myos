#ifndef MYOS_SETTINGS_H
#define MYOS_SETTINGS_H
#include <stdint.h>
typedef struct {
    char theme[16];
    char username[16];
    char hostname[24];
    uint8_t notifications;
    uint8_t animations;
    uint8_t mouse_sensitivity;
    uint8_t clock_24h;
    uint16_t terminal_scrollback;
} settings_state_t;
void settings_init(void);
int settings_load(settings_state_t* out);
int settings_save(const settings_state_t* state);
int settings_format(uint32_t lba);
int settings_select(uint32_t lba);
int settings_persistent(void);
uint32_t settings_lba(void);
#endif
