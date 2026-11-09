#ifndef CSERVE_TASK_STORAGE_H
#define CSERVE_TASK_STORAGE_H

#include <stdbool.h>
#include <stddef.h>

#define MAX_TASKS 128
#define MAX_TITLE 120

struct Task {
    int id;
    bool active;
    char title[MAX_TITLE + 1];
};

struct TaskStorage {
    struct Task tasks[MAX_TASKS];
    int next_id;
};

enum TaskCreateResult {
    TASK_CREATE_OK,
    TASK_CREATE_FULL,
    TASK_CREATE_ID_EXHAUSTED
};

/* Initialize before use. Each context owns its tasks and monotonically growing
 * IDs. Callers must not mutate fields except to establish test fixtures.
 * create requires an already validated, NUL-terminated title <= MAX_TITLE bytes
 * and a non-NULL created output. It copies title and writes created on success;
 * on failure both storage and the output are unchanged.
 * Returned task pointers are borrowed until the next mutation of this context.
 * at returns NULL for inactive or out-of-range slots; iteration is slot order.
 * All context pointers must be non-NULL. No sockets, HTTP or JSON belong here. */
void task_storage_init(struct TaskStorage *storage);
enum TaskCreateResult task_storage_create(struct TaskStorage *storage,
                                          const char *title, const struct Task **created);
bool task_storage_delete(struct TaskStorage *storage, int id);
const struct Task *task_storage_at(const struct TaskStorage *storage, size_t index);

#endif
