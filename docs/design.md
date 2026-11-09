# CServe Pi v1 — architecture

## Objective
Native Linux C17 HTTP/1.1 server on a Raspberry Pi, serving a dashboard and task
REST API. Educational, dependency-free, with automated black-box tests.

## Design
- TCP listener: IPv4 POSIX `socket` -> `bind` -> `listen` -> `accept`.
- Sequential client handling; closes connection after each HTTP request.
- Parser: bounded headers and request body, Content-Length on POST, reject chunked.
- Router: `GET /`, `GET /health`, `GET /api/tasks`, `POST /api/tasks`,
  `DELETE /api/tasks/:id`. Paths are fixed, not arbitrary filesystem names.
- Task model: in-memory array of 128 `struct Task`, monotonically increasing IDs.
- HTML dashboard: static file at `web/index.html`, browser `fetch()` to same-origin API.
- HTTP returns explicit `Content-Length`, error codes, and JSON data.
- Access: default 127.0.0.1, optional 0.0.0.0 for LAN demonstrations only.

## Main risks and mitigations
- Untrusted input: bounded body, validated fields, no dynamic format strings.
- Cross-site scripting: dashboard uses `textContent`, never task-driven HTML.
- Public access: no auth/TLS; prohibit public port forwarding and production use.
- Single-thread throughput: acceptable only for a tiny LAN demo; adding concurrency
  requires a separate design, tests, and review.
- Memory-only state: explicit design, data is lost on restart.

## Done for v1
`make` compiles warning-free; `make test` verifies HTTP routes, JSON and errors,
including Unicode round-trip. A Raspberry Pi user can compile it natively.
