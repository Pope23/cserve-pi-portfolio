#include "task_storage.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { EXPECTED_CAPACITY = 128, EXPECTED_TITLE_BYTES = 120 };
static const char TITLE[] = "storage test";

static void assert_create_failure(struct TaskStorage *storage, enum TaskCreateResult expected) {
    const struct Task sentinel = {0};
    const struct Task *created = &sentinel;
    assert(task_storage_create(storage, TITLE, &created) == expected);
    assert(created == &sentinel);
}

static void test_contexts_and_reinitialization(void) {
    struct TaskStorage first, second;
    const struct Task *created = NULL;
    task_storage_init(&first);
    task_storage_init(&second);
    assert(task_storage_create(&first, TITLE, &created) == TASK_CREATE_OK);
    assert(created != NULL && created->id == 1);
    assert(task_storage_at(&second, 0) == NULL);
    assert(task_storage_create(&second, TITLE, &created) == TASK_CREATE_OK);
    assert(created->id == 1);
    assert(task_storage_delete(&first, 1));
    assert(task_storage_at(&second, 0) != NULL);
    task_storage_init(&second);
    assert(task_storage_at(&second, 0) == NULL);
    assert(task_storage_create(&second, TITLE, &created) == TASK_CREATE_OK);
    assert(created->id == 1);
}

static void test_title_is_copied(void) {
    struct TaskStorage storage;
    const struct Task *created = NULL;
    char title[EXPECTED_TITLE_BYTES + 1];
    memset(title, 'x', EXPECTED_TITLE_BYTES);
    title[EXPECTED_TITLE_BYTES] = '\0';
    task_storage_init(&storage);
    assert(task_storage_create(&storage, title, &created) == TASK_CREATE_OK);
    title[0] = 'y';
    assert(created != NULL && created->active);
    assert(strlen(created->title) == EXPECTED_TITLE_BYTES);
    assert(created->title[0] == 'x');
    assert(task_storage_at(&storage, 0) == created);
}

static void fill_storage(struct TaskStorage *storage) {
    const struct Task *created = NULL;
    for (int id = 1; id <= EXPECTED_CAPACITY; id++) {
        assert(task_storage_create(storage, TITLE, &created) == TASK_CREATE_OK);
        assert(created != NULL && created->id == id);
    }
}

static void test_capacity_and_slot_reuse(void) {
    struct TaskStorage storage;
    const struct Task *created = NULL;
    task_storage_init(&storage);
    fill_storage(&storage);
    assert_create_failure(&storage, TASK_CREATE_FULL);
    assert(task_storage_delete(&storage, 2));
    assert(!task_storage_delete(&storage, 2));
    assert(task_storage_at(&storage, 1) == NULL);
    assert(task_storage_create(&storage, TITLE, &created) == TASK_CREATE_OK);
    assert(created->id == EXPECTED_CAPACITY + 1);
    assert(task_storage_at(&storage, 1) == created);
    assert(task_storage_at(&storage, 0)->id == 1);
    assert(task_storage_at(&storage, 2)->id == 3);
}

static void test_id_exhaustion(void) {
    struct TaskStorage storage;
    const struct Task *created = NULL;
    task_storage_init(&storage);
    storage.next_id = INT32_MAX - 1;
    assert(task_storage_create(&storage, TITLE, &created) == TASK_CREATE_OK);
    assert(created->id == INT32_MAX - 1);
    assert_create_failure(&storage, TASK_CREATE_ID_EXHAUSTED);
    assert(storage.next_id == INT32_MAX);
    assert(task_storage_at(&storage, 0)->id == INT32_MAX - 1);
    for (size_t i = 1; i < EXPECTED_CAPACITY; i++)
        assert(task_storage_at(&storage, i) == NULL);
    assert(task_storage_delete(&storage, INT32_MAX - 1));
    assert_create_failure(&storage, TASK_CREATE_ID_EXHAUSTED);
    assert(task_storage_at(&storage, 0) == NULL);
}

static void test_full_precedes_id_exhaustion(void) {
    struct TaskStorage storage;
    task_storage_init(&storage);
    fill_storage(&storage);
    storage.next_id = INT32_MAX;
    assert_create_failure(&storage, TASK_CREATE_FULL);
    assert(storage.next_id == INT32_MAX);
    assert(task_storage_at(&storage, EXPECTED_CAPACITY - 1)->id == EXPECTED_CAPACITY);
}

static void test_index_and_delete_bounds(void) {
    struct TaskStorage storage;
    task_storage_init(&storage);
    assert(task_storage_at(&storage, 0) == NULL);
    assert(task_storage_at(&storage, EXPECTED_CAPACITY) == NULL);
    assert(task_storage_at(&storage, SIZE_MAX) == NULL);
    assert(!task_storage_delete(&storage, 0));
    assert(!task_storage_delete(&storage, -1));
    assert(!task_storage_delete(&storage, INT32_MAX));
}

int main(void) {
    test_contexts_and_reinitialization();
    test_title_is_copied();
    test_capacity_and_slot_reuse();
    test_id_exhaustion();
    test_full_precedes_id_exhaustion();
    test_index_and_delete_bounds();
    puts("Task storage: 6 cases OK");
    return 0;
}
