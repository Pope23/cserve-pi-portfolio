# CServe Pi

A small HTTP task API written in **C17** for **Linux and Raspberry Pi**. Built
as an educational backend project to demonstrate POSIX networking, bounded
input parsing, explicit resource ownership, modular design, and integration testing.

**For localhost and controlled LAN demonstrations. This is not a production
HTTP server and must not be exposed directly to the public Internet.**

## Features

- A browser dashboard for creating, listing, and deleting tasks.
- A JSON API and a health endpoint.
- Up to 128 tasks stored in memory, with monotonically increasing IDs.
- Task titles limited to 120 UTF-8 bytes, with JSON escaping and Unicode escape decoding.
- Bounded HTTP headers and bodies, explicit Content-Length, and error responses.
- Sequential IPv4 connections, closed after one request.
- Configurable bind address and port; loopback is the default.
- Python integration tests and independent C test programs.

## Architecture

```text
main -> server -> http_parser
               -> router -> task_storage
                         -> http_response
```

| Module | Responsibility |
| --- | --- |
| `src/main.c` | Parse and validate command-line configuration. |
| `src/server.c` | Own the listener, accepted connections, timeouts, and task context. |
| `src/http_parser.c` | Read one bounded request and validate framing. |
| `src/router.c` | Dispatch routes, validate JSON/IDs, serialize tasks, and serve the fixed dashboard. |
| `src/task_storage.c` | Own task slots and IDs; provide create, delete, and read access. |
| `src/http_response.c` | Format headers and send JSON, errors, and empty responses. |

Each reusable module has a header. Internal helpers remain private. The router
borrows connection descriptors; the server closes them. Storage has no HTTP or
socket dependency. See [the design](docs/design.md) for contracts and ownership.

## Requirements

- Linux, including Raspberry Pi OS on ARM64.
- GCC or Clang with C17 support, GNU-compatible linker, and Make.
- Python 3 with its standard library for integration tests.
- Optional: curl for examples, a browser for the dashboard, and compiler
  sanitizer runtimes for ASan/UBSan.

The server has no external C library, database, or framework dependency. Tests
use GNU linker wrapping for deterministic system-call failures and Linux `/proc`
for descriptor checks. No Python packages are required to run the test suite.

## Build and run

Run commands from the repository root; the dashboard is loaded from
`web/index.html` relative to the working directory.

```sh
make                         # Release build with GCC and strict warnings
./build/cserve               # http://127.0.0.1:8080
```

To select Clang or a different local port:

```sh
make clean && make CC=clang
./build/cserve --host 127.0.0.1 --port 8081
```

Open `http://127.0.0.1:8080/` for the dashboard when using the default port.
For an explicit, controlled LAN demo only:

```sh
./build/cserve --host 0.0.0.0 --port 8080
```

Access the device using its own LAN address. Do not configure public port
forwarding. The CLI accepts only the loopback or all-interface bind address.
Stop the server with Ctrl+C; this terminates the process rather than draining requests.

## API

| Method | Path | Success response |
| --- | --- | --- |
| GET | `/` | 200, HTML dashboard |
| GET | `/health` | 200, `{"status":"ok"}` |
| GET | `/api/tasks` | 200, `{"tasks":[]}` or the current tasks |
| POST | `/api/tasks` | 201, `{"id":1,"title":"Learn sockets"}` |
| DELETE | `/api/tasks/1` | 204, empty body |

The ID shown assumes a fresh server. Creation accepts exactly one JSON field,
`title`, containing a nonblank string. Titles are limited in **bytes**, not
characters. Tasks disappear when the process stops.

```sh
curl -i http://127.0.0.1:8080/health
curl -i http://127.0.0.1:8080/api/tasks
curl -i http://127.0.0.1:8080/api/tasks \
  -H 'Content-Type: application/json' \
  --data '{"title":"Learn sockets"}'
curl -i -X DELETE http://127.0.0.1:8080/api/tasks/1
```

Errors include 400 for invalid input, 404 for unknown resources or invalid task
IDs, 405 for unsupported methods on `/api/tasks`, 411 for POST without
Content-Length, 413 for oversized requests, 500 for internal errors, and 507
when storage is full. The implementation preserves its documented HTTP subset;
method handling is not a general-purpose REST framework.

## Tests and debug builds

```sh
make clean && make CC=gcc && make CC=gcc test
make clean && make CC=clang && make CC=clang test
make debug && make test
```

Pass `CC` to both Make invocations: compiler selection does not persist across
commands. Release uses `-O2`; `make debug` rebuilds the server with debug symbols,
`-O0`, AddressSanitizer and UndefinedBehaviorSanitizer. It does not automatically
instrument test binaries built by a later Make invocation.

For a fully instrumented server and all C tests:

```sh
make clean
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 make CC=clang CFLAGS='-std=c17 -O1 -g -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror -D_POSIX_C_SOURCE=200809L -fsanitize=address,undefined -fno-omit-frame-pointer' test
```

`make test` runs **41 Python test methods and 26 C scenario groups** across four
programs. Integration tests start the real server on temporary loopback ports.
Coverage includes CRUD, JSON/Unicode, exact headers, 204 on the wire, malformed
and oversized requests, storage capacity and ID exhaustion, socket failures,
EINTR/partial transfers, descriptor cleanup, CLI errors, timeouts, and signals.
Injected errno messages from the lifecycle tests are expected.

Native GCC/Clang and sanitizer runs have passed on a Raspberry Pi 5 ARM64.
No benchmark or throughput claim is made. The attempted browser-check status and its limitations are recorded in
[release readiness](docs/release-readiness.md).

## Continuous integration

[The workflow](.github/workflows/ci.yml) targets GitHub-hosted Ubuntu on pushes
to `main` and pull requests targeting `main`. It builds and tests a GCC/Clang
matrix, and runs a separate Clang ASan/UBSan job with all binaries instrumented.
It uses read-only repository permissions and never relies on a public Raspberry
Pi runner. Remote CI results remain unverified until the repository is published
and the workflow actually runs; local commands have been validated separately.

## Limits and security

- No authentication, authorization, HTTPS, persistence, or concurrency.
- HTTP/1.1 subset: no keep-alive, chunked uploads, or full RFC compliance.
- Headers: 8192 bytes; body: 4096 bytes; task title: 120 bytes.
- Five-second recv/send inactivity timeouts are not a total request deadline.
  Socket-option failures are ignored; a slow client can monopolize the server.
- Direct UTF-8 input is not fully validated. The dashboard input length also
  does not measure UTF-8 bytes; the server enforces its own byte limit.
- Dashboard task text uses `textContent`, never untrusted `innerHTML`.
- Transport errors are not propagated through the response API.
- SIGTERM/SIGINT use default termination. The kernel releases descriptors,
  but application cleanup and normal exit-time LeakSanitizer checks are bypassed.
  No LSan report after a signal is not proof that no leaks exist.

See [the technical audit](docs/stage-7-results.md) and
[release readiness](docs/release-readiness.md) for evidence and remaining limits.

## License

[MIT](LICENSE), copyright 2026 Pope23. No third-party implementation is bundled
in the inspected source tree. Compilers, system libraries, browsers, Python, and
GitHub Actions retain their own licenses; the project license does not relicense
those tools. See the provenance scope in the release-readiness report.
