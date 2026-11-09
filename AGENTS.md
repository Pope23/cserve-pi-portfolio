# CServe Pi — agent instructions

## Project goal
A minimal and educational HTTP/1.1 task API server in C17, built and run natively
on Raspberry Pi OS (Linux). Keep it understandable, safe for localhost/LAN demos,
and strong enough to present in an entry-level C/backend interview.

## Workflow
- Before changes, inspect `README.md`, `docs/design.md`, the affected source and tests.
- Use installed Superpowers skills where available: brainstorming, writing-plans,
  test-driven-development, systematic-debugging, requesting-code-review,
  verification-before-completion.
- For behavior changes, use RED → GREEN → REFACTOR: first add a failing integration
  test, run `make test` and observe failure; implement minimal code; rerun all tests.
- Do not add dependencies, threading, databases, or frameworks without an explicit
  scope decision and a documented reason.
- Never claim tests passed on a Raspberry Pi unless actually run there.

## Code quality
- C17, POSIX APIs, `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`.
- These rules apply directly to Codex; do not rely on automatic loading of
  `.cursor/rules/clean-code.mdc`. Keep that file and this section consistent.
- No hardcoded strings or magic numbers for configuration, routes, protocol
  metadata, messages, capacities, or limits: use named constants at the owning
  module. Derive lengths from those constants instead of duplicating values.
  Ordinary syntax characters and obvious 0/1 operations may remain literals;
  name encoding masks and boundaries where that explains their meaning.
- No duplicated logic: extract cohesive shared helpers, including in existing
  code touched by a refactor. Do not merge different validation policies merely
  because they look similar.
- Functions should have one responsibility and aim for approximately 20 lines
  where reasonable. Never fragment functions artificially to meet a line count.
- Cyclomatic complexity must be at most 20 per function, with a target below 10.
  Review branches manually; report measured scores only when an analysis tool
  was actually run, identifying its method and limitations.
- Keep C17 portable on Linux ARM64 using POSIX sockets. Avoid assumptions about
  integer widths, signedness, alignment, and host/network byte order.
- Validate all external input and enforce bounds before copying bytes.
- Check capacity, terminators, integer overflow and signed/unsigned conversions
  before buffer arithmetic, parsing, formatting, and copying.
- Do not change API behavior without tests. For behavior changes follow the
  RED → GREEN → REFACTOR workflow above; for structural refactors preserve
  passing characterization tests before and after each small extraction.
- Never insert untrusted text into the frontend as `innerHTML`.
- Keep defaults loopback-only. Do not expose an unauthenticated API to the Internet.

## Commands
- Compile: `make`
- Full suite: `make test`
- Clean: `make clean`
- Debug binary: `make debug` (may require sanitizer runtime)
- Run locally: `./build/cserve`
- Local-network demo: `./build/cserve --host 0.0.0.0 --port 8080`
