#include <stdint.h>
#include <stddef.h>

extern void* kmalloc(size_t size);
extern void switch_task(uint32_t* old_esp, uint32_t new_esp);

typedef struct task {
    uint32_t esp;
    uint32_t id;
    struct task* next;
} task_t;

static task_t* current_task = NULL;
static task_t* task_head = NULL;
static uint32_t next_pid = 1;

// Global counters modified by background tasks
volatile uint32_t task1_counter = 0;
volatile uint32_t task2_counter = 0;

void create_task(void (*entry_point)()) {
    task_t* new_task = (task_t*)kmalloc(sizeof(task_t));
    new_task->id = next_pid++;

    // Allocate a dedicated 4KB stack space for this task
    uint8_t* stack = (uint8_t*)kmalloc(4096);
    uint32_t* esp = (uint32_t*)(stack + 4096);

    // Push entry point address for the 'ret' instruction in switch_task
    *(--esp) = (uint32_t)entry_point;

    // Push 8 dummy values representing zeroed initial general registers (for popa)
    for (int i = 0; i < 8; i++) {
        *(--esp) = 0;
    }

    new_task->esp = (uint32_t)esp;

    // Maintain circular linked list of tasks
    if (task_head == NULL) {
        task_head = new_task;
        new_task->next = new_task;
        current_task = new_task;
    } else {
        task_t* temp = task_head;
        while (temp->next != task_head) {
            temp = temp->next;
        }
        temp->next = new_task;
        new_task->next = task_head;
    }
}

void schedule() {
    if (!current_task || !current_task->next) return;

    task_t* old_task = current_task;
    current_task = current_task->next;

    if (old_task != current_task) {
        switch_task(&old_task->esp, current_task->esp);
    }
}

// Background Task 1
void task1_routine() {
    while (1) {
        task1_counter++;
        schedule(); // Yield CPU back to scheduler
    }
}

// Background Task 2
void task2_routine() {
    while (1) {
        task2_counter++;
        schedule(); // Yield CPU back to scheduler
    }
}

void init_multitasking() {
    // Initialize main kernel thread context
    task_t* main_task = (task_t*)kmalloc(sizeof(task_t));
    main_task->id = 0;
    main_task->next = main_task;
    task_head = main_task;
    current_task = main_task;

    // Spawn background tasks
    create_task(task1_routine);
    create_task(task2_routine);
}