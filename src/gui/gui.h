#ifndef MYOS_GUI_H
#define MYOS_GUI_H

#include <stdint.h>

void gui_init(void);
int gui_available(void);
void gui_set_theme(const char* name);
void gui_terminal_clear(void);
void gui_terminal_begin_input(void);
void gui_terminal_edit(const char* text, int len, int cursor_pos, uint8_t shell_color);
void gui_terminal_set_cursor(int cursor_pos);
void gui_terminal_putchar(char c, uint8_t shell_color);
void gui_redraw(void);
void gui_present(void);
void gui_mouse_event(int dx, int dy, int wheel, uint8_t buttons);

#endif
