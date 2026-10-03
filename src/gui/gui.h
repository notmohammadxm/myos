#ifndef MYOS_GUI_H
#define MYOS_GUI_H

#include <stdint.h>

typedef struct {
    int type;
    const char* value;
    int arg;
} gui_action_t;

typedef struct {
    int pid;
    const char* name;
    const char* state;
    int cpu_percent;
    int memory_kib;
    int priority;
} gui_task_info_t;

typedef struct {
    int index;
    char name[48];
    int directory;
    uint32_t size;
} gui_file_info_t;

typedef struct {
    int available;
    int link_up;
    uint8_t mac[6];
    uint32_t ip;
    uint32_t gateway;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t ping_success;
    uint32_t ping_fail;
} gui_net_info_t;

#define GUI_ACTION_THEME 1
#define GUI_ACTION_REBOOT 2
#define GUI_ACTION_SHUTDOWN 3
#define GUI_ACTION_TASK_TERMINATE 4
#define GUI_ACTION_TASK_RESTART 5
#define GUI_ACTION_FILE_DELETE 6
#define GUI_ACTION_FILE_CREATE 7
#define GUI_ACTION_DIR_CREATE 8
#define GUI_ACTION_FILE_OPEN 9
#define GUI_ACTION_NET_PING 10

#define GUI_ACTION_NONE 0

void gui_init(void);
int gui_available(void);
int gui_keyboard_event(int event, int ctrl, int shift);
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
void gui_taskmgr_set_data(const gui_task_info_t* tasks, int count);
void gui_taskmgr_open(void);
void gui_filemgr_set_data(const gui_file_info_t* files, int count);
void gui_filemgr_open(void);
void gui_network_set_status(const gui_net_info_t* status);
void gui_network_open(void);
#ifdef UNIT_TEST
int gui_test_calculate(const char* expression, int32_t* result);
#endif
int gui_take_terminal_cursor(int* cursor_pos);
int gui_poll_action(gui_action_t* action);

#endif
