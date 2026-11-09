#ifndef CSERVE_ROUTER_H
#define CSERVE_ROUTER_H

struct Request;
struct TaskStorage;

/* Borrows fd and a successfully parsed Request. Storage is initialized by the
 * caller and lives across requests. Emits one response without closing fd.
 * JSON validation/serialization and the fixed dashboard path stay private. */
void router_handle(int fd, const struct Request *req, struct TaskStorage *storage);

#endif
