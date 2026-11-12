#include "mouse.h"

#define KBD_STATUS 0x64
#define KBD_DATA   0x60
#define MOUSE_IRQ_BUFFER 64

static volatile uint8_t packet_bytes[4];
static volatile uint8_t packet_index;
static volatile uint8_t packet_size = 3;
static volatile mouse_event_t queue[MOUSE_IRQ_BUFFER];
static volatile uint8_t head;
static volatile uint8_t tail;
static volatile uint8_t present;
static volatile int32_t x;
static volatile int32_t y;
static volatile uint8_t buttons;
static volatile int32_t max_x = 1023;
static volatile int32_t max_y = 767;
static volatile uint8_t sensitivity = 100;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void io_wait(void) { outb(0x80, 0); }
static void wait_input_clear(void) {
    for (uint32_t i = 0; i < 100000u; ++i)
        if (!(inb(KBD_STATUS) & 0x02u)) return;
}
static int wait_output_full(void) {
    for (uint32_t i = 0; i < 100000u; ++i)
        if (inb(KBD_STATUS) & 0x01u) return 1;
    return 0;
}
static void drain_output(void) {
    for (uint32_t i = 0; i < 32u; ++i) {
        if (!(inb(KBD_STATUS) & 0x01u)) break;
        (void)inb(KBD_DATA);
    }
}
static int aux_write(uint8_t value) {
    wait_input_clear();
    outb(KBD_STATUS, 0xD4);
    wait_input_clear();
    outb(KBD_DATA, value);
    return 1;
}
static int aux_read(uint8_t* out) {
    if (!out || !wait_output_full()) return 0;
    uint8_t status = inb(KBD_STATUS);
    if (!(status & 0x20u)) {
        (void)inb(KBD_DATA);
        return 0;
    }
    *out = inb(KBD_DATA);
    return 1;
}
static int aux_command(uint8_t command, uint8_t value) {
    uint8_t ack = 0;
    if (!aux_write(command) || !aux_read(&ack) || ack != 0xFAu) return 0;
    if (!aux_write(value) || !aux_read(&ack) || ack != 0xFAu) return 0;
    return 1;
}
static void queue_event(int32_t dx, int32_t dy, int32_t wheel, uint8_t new_buttons) {
    if (sensitivity < 25) sensitivity = 25;
    if (sensitivity > 200) sensitivity = 200;
    dx = (dx * (int32_t)sensitivity) / 100;
    dy = (dy * (int32_t)sensitivity) / 100;
    x += dx;
    y += dy;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > max_x) x = max_x;
    if (y > max_y) y = max_y;
    buttons = new_buttons;

    uint8_t next = (uint8_t)((head + 1u) % MOUSE_IRQ_BUFFER);
    if (next == tail) {
        /* Keep the newest button state and motion rather than freezing input
         * when the consumer briefly falls behind the interrupt stream. */
        tail = (uint8_t)((tail + 1u) % MOUSE_IRQ_BUFFER);
    }
    queue[head].dx = dx;
    queue[head].dy = dy;
    queue[head].wheel = wheel;
    queue[head].buttons = new_buttons;
    head = next;
}

int mouse_init(void) {
    present = 0;
    packet_index = 0;
    packet_size = 3;
    head = tail = 0;
    x = 0;
    y = 0;
    buttons = 0;
    drain_output();

    wait_input_clear();
    outb(KBD_STATUS, 0xA8); /* enable auxiliary device */
    io_wait();

    wait_input_clear();
    outb(KBD_STATUS, 0x20); /* read controller configuration byte */
    if (!wait_output_full()) return 0;
    uint8_t config = inb(KBD_DATA);
    config |= 0x02u; /* IRQ12 */
    config &= (uint8_t)~0x20u; /* enable mouse clock */

    wait_input_clear();
    outb(KBD_STATUS, 0x60);
    wait_input_clear();
    outb(KBD_DATA, config);

    /* IntelliMouse wheel negotiation: the 200/100/80 sample-rate sequence
     * enables a four-byte packet on compatible devices. Older mice remain
     * supported and continue using the standard three-byte packet. */
    int wheel_mouse = 0;
    if (aux_command(0xF3u, 200u) &&
        aux_command(0xF3u, 100u) &&
        aux_command(0xF3u, 80u)) {
        uint8_t ack = 0, id = 0;
        if (aux_write(0xF2u) && aux_read(&ack) && ack == 0xFAu &&
            aux_read(&id) && (id == 0x03u || id == 0x04u))
            wheel_mouse = 1;
    }
    packet_size = wheel_mouse ? 4u : 3u;

    /* Optional tuning is non-fatal for older or non-standard devices. */
    (void)aux_command(0xF3u, 200u);
    (void)aux_command(0xE8u, 2u);

    if (!aux_write(0xF4)) return 0; /* enable streaming */
    uint8_t ack = 0;
    if (!aux_read(&ack) || ack != 0xFAu) return 0;

    present = 1;
    x = 0;
    y = 0;
    return 1;
}

void mouse_set_bounds(uint32_t width, uint32_t height) {
    max_x = width ? (int32_t)(width - 1u) : 0;
    max_y = height ? (int32_t)(height - 1u) : 0;
    if (x > max_x) x = max_x;
    if (y > max_y) y = max_y;
}

int mouse_available(void) { return present != 0; }

int mouse_poll(mouse_event_t* event) {
    if (!event || tail == head) return 0;
    *event = queue[tail];
    tail = (uint8_t)((tail + 1u) % MOUSE_IRQ_BUFFER);
    return 1;
}

void mouse_set_sensitivity(uint8_t percent) {
    if (percent < 25u) percent = 25u;
    if (percent > 200u) percent = 200u;
    sensitivity = percent;
}

uint8_t mouse_get_sensitivity(void) { return sensitivity; }

int32_t mouse_x(void) { return x; }
int32_t mouse_y(void) { return y; }
uint8_t mouse_buttons(void) { return buttons; }

static void process_mouse_packet(void) {
    if (packet_bytes[0] & 0xC0u) return; /* overflow: discard packet */
    int32_t dx = (int8_t)packet_bytes[1];
    int32_t dy = -(int32_t)(int8_t)packet_bytes[2];
    int32_t wheel = 0;
    if (packet_size == 4u) {
        uint8_t nibble = packet_bytes[3] & 0x0Fu;
        wheel = (nibble & 0x08u) ? (int32_t)nibble - 16 : (int32_t)nibble;
    }
    /* Convert PS/2 +Y (physical up) to screen +Y (down). */
    queue_event(dx, dy, wheel, packet_bytes[0] & 0x07u);
}

void mouse_irq_handler(void) {
    uint8_t status = inb(KBD_STATUS);
    if (!(status & 0x01u) || !(status & 0x20u)) return;
    uint8_t value = inb(KBD_DATA);

    if (packet_index == 0 && !(value & 0x08u)) return; /* lost sync */
    packet_bytes[packet_index++] = value;
    if (packet_index < packet_size) return;
    packet_index = 0;
    process_mouse_packet();
}

#ifdef UNIT_TEST
void mouse_test_reset(void) {
    packet_index = 0;
    packet_size = 3;
    head = tail = 0;
    x = y = 0;
    buttons = 0;
    max_x = 1023;
    max_y = 767;
    sensitivity = 100;
}

void mouse_test_feed_wheel_packet(uint8_t flags, uint8_t dx, uint8_t dy, uint8_t wheel) {
    packet_size = 4;
    packet_bytes[0] = flags;
    packet_bytes[1] = dx;
    packet_bytes[2] = dy;
    packet_bytes[3] = wheel;
    process_mouse_packet();
}
#endif
