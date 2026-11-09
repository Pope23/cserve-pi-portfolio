# CServe Pi — next iterations (choose one at a time)

1. **Persistence**: JSON-lines or atomic file storage; write tests for reboot
   and corrupted data before implementation.
2. **Raspberry Pi metrics**: `GET /api/system` serving CPU temperature, uptime,
   and memory info using documented Linux interfaces. Test error cases, fallback
   behavior, and avoid shell command injection.
3. **Observability**: structured request logs, response latency and `/metrics`.
4. **Concurrent requests**: non-blocking sockets with `poll()` and benchmark;
   establish resource limits and backpressure.
5. **Security**: restrict bind address, authenticated access, proxy-based TLS,
   request timeout hardening and rate limiting before Internet exposure.
6. **Automation**: CI on Linux and ARM builds; systemd smoke testing on Pi.

Work through one slice with Superpowers: agree on a design, plan small tasks,
write failing tests, implement, review and document results.
