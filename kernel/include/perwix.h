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
bool usb_xhci_ready(void);
uint32_t usb_connected_port_count(void);

void keyboard_init(void);
bool keyboard_read_char(char *character);
bool mouse_init(void);
bool mouse_has_wheel(void);
void mouse_poll(void);
void shell_run(void);

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
