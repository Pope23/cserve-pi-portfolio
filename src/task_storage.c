#include "task_storage.h"
#include <stdint.h>
#include <string.h>

void task_storage_init(struct TaskStorage *storage) {
    *storage = (struct TaskStorage){.next_id = 1};
}

enum TaskCreateResult task_storage_create(struct TaskStorage *storage,
                                          const char *title, const struct Task **created) {
    for (size_t i = 0; i < MAX_TASKS; i++) {
        struct Task *task = &storage->tasks[i];
        if (task->active) continue;
        if (storage->next_id == INT32_MAX) return TASK_CREATE_ID_EXHAUSTED;
        task->id = storage->next_id++;
        task->active = true;
        memcpy(task->title, title, strlen(title) + 1U);
        *created = task;
        return TASK_CREATE_OK;
    }
    return TASK_CREATE_FULL;
}

bool task_storage_delete(struct TaskStorage *storage, int id) {
    for (size_t i = 0; i < MAX_TASKS; i++) {
        struct Task *task = &storage->tasks[i];
        if (task->active && task->id == id) {
            task->active = false;
            return true;
        }
    }
    return false;
}

const struct Task *task_storage_at(const struct TaskStorage *storage, size_t index) {
    if (index >= MAX_TASKS || !storage->tasks[index].active) return NULL;
    return &storage->tasks[index];
}
