#include "perwix.h"

typedef struct {
    ipc_message_t items[PERWIX_IPC_CAPACITY];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} ipc_queue_t;

static ipc_queue_t queues[PERWIX_TASK_COUNT];

void ipc_init(void)
{
    for (uint32_t i = 0; i < PERWIX_TASK_COUNT; ++i) {
        queues[i].head = 0;
        queues[i].tail = 0;
        queues[i].count = 0;
    }
}

bool ipc_send(uint32_t receiver, ipc_message_t message)
{
    if (receiver >= PERWIX_TASK_COUNT) {
        return false;
    }

    uint64_t flags = irq_save();
    ipc_queue_t *queue = &queues[receiver];
    if (queue->count == PERWIX_IPC_CAPACITY) {
        irq_restore(flags);
        return false;
    }

    queue->items[queue->tail] = message;
    queue->tail = (queue->tail + 1) % PERWIX_IPC_CAPACITY;
    ++queue->count;
    irq_restore(flags);
    return true;
}

bool ipc_receive(uint32_t receiver, ipc_message_t *message)
{
    if (receiver >= PERWIX_TASK_COUNT || message == NULL) {
        return false;
    }

    uint64_t flags = irq_save();
    ipc_queue_t *queue = &queues[receiver];
    if (queue->count == 0) {
        irq_restore(flags);
        return false;
    }

    *message = queue->items[queue->head];
    queue->head = (queue->head + 1) % PERWIX_IPC_CAPACITY;
    --queue->count;
    irq_restore(flags);
    return true;
}
