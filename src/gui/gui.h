#ifndef MYOS_GUI_H
#define MYOS_GUI_H

#include <stdint.h>

typedef struct {
    int type;
    const char* value;
} gui_action_t;

#define GUI_ACTION_NONE 0
#define GUI_ACTION_THEME 1
#define GUI_ACTION_REBOOT 2
#define GUI_ACTION_SHUTDOWN 3

void gui_init(void);
int gui_available(void);
void gui_set_theme(const char* name);
void gui_terminal_clear(void);
void gui_terminal_begin_input(void);
void gui_terminal_edit(const char* text, int len, int cursor_pos, uint8_t shell_color);
void gui_terminal_set_cursor(int cursor_pos);
void gui_terminal_set_selection(int anchor, int cursor_pos);
void gui_terminal_scroll(int rows);
void gui_terminal_putchar(char c, uint8_t shell_color);
void gui_redraw(void);
void gui_present(void);
void gui_mouse_event(int dx, int dy, int wheel, uint8_t buttons);
void gui_set_runtime_ticks(uint32_t ticks);
void gui_set_clock(int hour, int minute, int second, int day, int month, int year);
void gui_open_window(int index);
#ifdef UNIT_TEST
int gui_test_calculate(const char* expression, int32_t* result);
#endif
int gui_take_terminal_cursor(int* cursor_pos);
int gui_poll_action(gui_action_t* action);

#endif
