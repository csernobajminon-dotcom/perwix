#ifndef PERWIX_H
#define PERWIX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PERWIX_TASK_COUNT 3
#define PERWIX_IPC_CAPACITY 16
#define PERWIX_TIMER_HZ 100

void console_init(void);
void console_write(const char *text);
void console_draw_logo(void);
void console_write_logo_row(uint32_t row, uint32_t width, uint32_t height);
void console_clear(void);
void console_set_color(uint8_t foreground);
void console_scroll_view(int32_t lines);
uint32_t console_scrollback_lines(void);
void console_write_u64(uint64_t value);
void console_write_hex(uint64_t value);

void interrupts_init(void);
void timer_init(void);
void irq_enable(void);
uint64_t irq_save(void);
void irq_restore(uint64_t flags);
uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t value);
uint16_t inw(uint16_t port);
void outw(uint16_t port, uint16_t value);
uint32_t inl(uint16_t port);
void outl(uint16_t port, uint32_t value);
uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function,
                           uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t device, uint8_t function,
                        uint8_t offset, uint32_t value);
bool net_init(void);
bool net_ready(void);
bool net_ping(uint32_t ipv4_address);
uint32_t net_received_frames(void);
bool usb_init(void);
uint32_t usb_controller_count(void);
uint32_t usb_controller_ready_count(void);
uint32_t usb_controller_family_count(uint8_t programming_interface);
uint32_t usb_controller_family_ready_count(uint8_t programming_interface);
bool usb_xhci_ready(void);
uint32_t usb_connected_port_count(void);

void keyboard_init(void);
enum {
    PERWIX_KEY_NONE = 0,
    PERWIX_KEY_CHAR = 1,
    PERWIX_KEY_UP = 2,
    PERWIX_KEY_DOWN = 3,
    PERWIX_KEY_LEFT = 4,
    PERWIX_KEY_RIGHT = 5,
    PERWIX_KEY_ESCAPE = 6
};
uint8_t keyboard_read_key(char *character);
uint8_t keyboard_read_codepoint(uint32_t *codepoint);
const char *keyboard_layout_name(void);
bool keyboard_read_char(char *character);
bool mouse_init(void);
bool mouse_has_wheel(void);
void mouse_poll(void);
void shell_run(void);
void extra_help(void);
bool extra_command_run(const char *command, char *argument);

void pci_init(void);
uint32_t pci_device_count(void);
bool pci_device_get(uint32_t index, uint8_t *bus, uint8_t *device,
                    uint8_t *function, uint16_t *vendor_id,
                    uint16_t *device_id, uint8_t *class_code,
                    uint8_t *subclass, uint8_t *programming_interface);
const char *usb_controller_name(uint8_t class_code, uint8_t subclass,
                                uint8_t programming_interface);

void pmm_init(uint64_t multiboot_info_address);
uint64_t pmm_bootstrap_pages(void);
void *pmm_alloc_page(void);
void *pmm_alloc_pages(uint32_t count);   /* contiguous, zeroed */
void net_poll(void);
bool parse_ipv4_text(const char *text, uint32_t *address);
void console_write_ipv4(uint32_t address);
void console_write_mac(const uint8_t mac[6]);

typedef struct {
    uint32_t from;
    uint32_t type;
    uint64_t value;
} ipc_message_t;

void ipc_init(void);
bool ipc_send(uint32_t receiver, ipc_message_t message);
bool ipc_receive(uint32_t receiver, ipc_message_t *message);

void scheduler_init(void);
uint64_t *scheduler_tick(uint64_t *saved_stack);
uint32_t scheduler_current_id(void);
const char *scheduler_task_name(uint32_t id);
uint64_t timer_handler(uint64_t *saved_stack);
void default_interrupt(void);
extern volatile uint64_t system_ticks;

#endif
