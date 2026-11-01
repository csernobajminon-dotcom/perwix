#include "perwix.h"

#define TASK_STACK_SIZE 8192

typedef struct {
    uint64_t *saved_stack;
    uint8_t stack[TASK_STACK_SIZE] __attribute__((aligned(16)));
    const char *name;
    void (*entry)(void);
} task_t;

static task_t tasks[PERWIX_TASK_COUNT];
static uint32_t current_task;
volatile uint64_t system_ticks;

static void task_bootstrap(void);
static void sender_task(void);
static void receiver_task(void);

static uint64_t *make_initial_stack(task_t *task)
{
    uint64_t *stack_top =
        (uint64_t *)(uintptr_t)(task->stack + sizeof(task->stack));
    stack_top = (uint64_t *)((uintptr_t)stack_top & ~0xfULL);
    uint64_t task_stack_pointer = (uint64_t)(uintptr_t)stack_top - 8;

    *--stack_top = 0x10;
    *--stack_top = task_stack_pointer;
    *--stack_top = 0x202;
    *--stack_top = 0x08;
    *--stack_top = (uint64_t)(uintptr_t)task_bootstrap;
    for (uint32_t i = 0; i < 15; ++i) {
        *--stack_top = 0;
    }
    return stack_top;
}

void scheduler_init(void)
{
    current_task = 0;
    system_ticks = 0;

    tasks[0].name = "kernel-main";
    tasks[0].entry = NULL;
    tasks[0].saved_stack = NULL;

    tasks[1].name = "ipc-sender";
    tasks[1].entry = sender_task;
    tasks[1].saved_stack = make_initial_stack(&tasks[1]);

    tasks[2].name = "ipc-receiver";
    tasks[2].entry = receiver_task;
    tasks[2].saved_stack = make_initial_stack(&tasks[2]);
}

uint64_t *scheduler_tick(uint64_t *saved_stack)
{
    ++system_ticks;
    tasks[current_task].saved_stack = saved_stack;

    for (uint32_t step = 1; step <= PERWIX_TASK_COUNT; ++step) {
        uint32_t candidate = (current_task + step) % PERWIX_TASK_COUNT;
        if (tasks[candidate].saved_stack != NULL) {
            current_task = candidate;
            return tasks[candidate].saved_stack;
        }
    }
    return saved_stack;
}

uint32_t scheduler_current_id(void)
{
    return current_task;
}

const char *scheduler_task_name(uint32_t id)
{
    if (id >= PERWIX_TASK_COUNT) {
        return "invalid";
    }
    return tasks[id].name;
}

static void task_bootstrap(void)
{
    uint32_t id = scheduler_current_id();
    tasks[id].entry();
    for (;;) {
        __asm__ volatile("hlt");
    }
}

static void sender_task(void)
{
    uint64_t last_tick = 0;
    uint64_t sequence = 0;
    for (;;) {
        uint64_t now = system_ticks;
        if (now - last_tick >= 50) {
            last_tick = now;
            ipc_message_t message = {
                .from = 1,
                .type = 1,
                .value = ++sequence,
            };
            (void)ipc_send(2, message);
        }
        __asm__ volatile("pause");
    }
}

static void receiver_task(void)
{
    for (;;) {
        ipc_message_t message;
        (void)ipc_receive(2, &message);
        __asm__ volatile("pause");
    }
}

uint64_t timer_handler(uint64_t *saved_stack)
{
    outb(0x20, 0x20);
    uint64_t *next_stack = scheduler_tick(saved_stack);
    return (uint64_t)(uintptr_t)next_stack;
}
