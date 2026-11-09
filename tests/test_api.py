"""Black-box integration tests of the real C server using only Python stdlib."""
import http.client
from http import HTTPStatus
import json
import os
from pathlib import Path
import socket
import subprocess
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "build" / "cserve"
TASKS_PATH = "/api/tasks"
HEALTH_PATH = "/health"
JSON_MIME = "application/json; charset=utf-8"
EMPTY_JSON_MIME = "application/json"
OVERSIZED_BODY_BYTES = 5000
INVALID_TASK_IDS = ("", "0", "-1", "abc", "2147483648")
SOCKET_TIMEOUT_SECONDS = 2
SOCKET_READ_BYTES = 4096
MAX_RESPONSE_BYTES = 65536
TASK_CAPACITY = 128
HEADER_LIMIT_BYTES = 8192
BODY_LIMIT_BYTES = 4096
TITLE_LIMIT_BYTES = 120


def unused_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class CServeApiTests(unittest.TestCase):
    def setUp(self):
        self.port = unused_port()
        self.process = subprocess.Popen(
            [str(BINARY), "--host", "127.0.0.1", "--port", str(self.port)],
            cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        )
        for _ in range(80):
            if self.process.poll() is not None:
                self.fail("C server terminated during startup")
            try:
                with socket.create_connection(("127.0.0.1", self.port), timeout=0.1):
                    break
            except OSError:
                time.sleep(0.025)
        else:
            self.fail("C server did not listen within 2 seconds")

    def tearDown(self):
        if self.process.poll() is None:
            self.process.terminate()
        try:
            self.process.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.communicate()

    def call(self, method, path, data=None, headers=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=2)
        try:
            body = None if data is None else json.dumps(data, ensure_ascii=False).encode("utf-8")
            outgoing = {"Content-Type": "application/json"} if body else {}
            outgoing.update(headers or {})
            conn.request(method, path, body=body, headers=outgoing)
            res = conn.getresponse()
            return res.status, res.read(), dict(res.getheaders())
        finally:
            conn.close()

    def test_health(self):
        status, body, _ = self.call("GET", "/health")
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body), {"status": "ok"})
        self.assertEqual(body, b'{"status":"ok"}')

    def assert_response_metadata(self, body, headers, mime=JSON_MIME):
        self.assertEqual(headers["Content-Type"], mime)
        self.assertEqual(headers["Content-Length"], str(len(body)))
        self.assertEqual(headers["Connection"], "close")
        self.assertEqual(headers["Cache-Control"], "no-store")
        self.assertEqual(headers["X-Content-Type-Options"], "nosniff")

    def raw_response(self, request, *, shutdown_write=False):
        with socket.create_connection(("127.0.0.1", self.port),
                                      timeout=SOCKET_TIMEOUT_SECONDS) as conn:
            chunks = (request,) if isinstance(request, bytes) else request
            for chunk in chunks:
                conn.sendall(chunk)
            if shutdown_write:
                conn.shutdown(socket.SHUT_WR)
            response = bytearray()
            while True:
                chunk = conn.recv(SOCKET_READ_BYTES)
                if not chunk:
                    return bytes(response)
                response.extend(chunk)
                self.assertLessEqual(len(response), MAX_RESPONSE_BYTES)

    def assert_raw_status(self, request, status, *, shutdown_write=False):
        wire = self.raw_response(request, shutdown_write=shutdown_write)
        header, body = wire.split(b"\r\n\r\n", 1)
        self.assertEqual(int(header.split(b" ", 2)[1]), status)
        headers = dict(line.decode("ascii").split(": ", 1)
                       for line in header.split(b"\r\n")[1:])
        self.assert_response_metadata(body, headers)
        return body

    def test_invalid_content_lengths(self):
        cases = (
            b"Content-Length: 0\r\nContent-Length: 0\r\n",
            b"Content-Length: -1\r\n",
            b"Content-Length: abc\r\n",
            b"Content-Length: \r\n",
            b"Content-Length: 18446744073709551616\r\n",
        )
        for headers in cases:
            with self.subTest(headers=headers):
                request = b"POST /api/tasks HTTP/1.1\r\n" + headers + b"\r\n"
                body = self.assert_raw_status(request, HTTPStatus.BAD_REQUEST)
                self.assertEqual(body, b'{"error":"invalid request"}')

    def test_transfer_encoding_rejected(self):
        self.assert_raw_status(
            b"POST /api/tasks HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n",
            HTTPStatus.BAD_REQUEST,
        )

    def test_body_length_limit(self):
        self.assert_raw_status(
            b"POST /api/tasks HTTP/1.1\r\nContent-Length: 4097\r\n\r\n",
            HTTPStatus.REQUEST_ENTITY_TOO_LARGE,
        )
        # A valid title with whitespace padding exercises exactly 4096 body bytes.
        body = b'{"title":"at body limit"}'
        body += b" " * (BODY_LIMIT_BYTES - len(body))
        request = b"POST /api/tasks HTTP/1.1\r\nContent-Length: 4096\r\n\r\n" + body
        result = self.assert_raw_status(request, HTTPStatus.CREATED)
        self.assertEqual(json.loads(result), {"id": 1, "title": "at body limit"})

    def test_unterminated_header_limit(self):
        prefix = b"GET /health HTTP/1.1\r\nX-Padding: "
        request = prefix + b"x" * (HEADER_LIMIT_BYTES - len(prefix))
        self.assert_raw_status(request, HTTPStatus.REQUEST_ENTITY_TOO_LARGE)

    def test_complete_header_limit(self):
        prefix = b"GET /health HTTP/1.1\r\nX-Padding: "
        suffix = b"\r\n\r\n"
        request = prefix + b"x" * (HEADER_LIMIT_BYTES - len(prefix) - len(suffix)) + suffix
        self.assert_raw_status(request, HTTPStatus.OK)
        # One additional padding byte puts the complete header over the limit.
        self.assert_raw_status(prefix + b"x" + request[len(prefix):],
                               HTTPStatus.REQUEST_ENTITY_TOO_LARGE)

    def test_fragmented_request(self):
        body = b'{"title":"fragmented request"}'
        chunks = (
            b"POST /api/", b"tasks HTTP/1.1\r\nContent-Len", b"gth: ",
            str(len(body)).encode("ascii"), b"\r\n\r", b"\n", body[:1], body[1:],
        )
        result = self.assert_raw_status(chunks, HTTPStatus.CREATED)
        self.assertEqual(json.loads(result), {"id": 1, "title": "fragmented request"})

    def test_nul_body_rejected(self):
        self.assert_raw_status(
            b"POST /api/tasks HTTP/1.1\r\nContent-Length: 1\r\n\r\n\x00",
            HTTPStatus.BAD_REQUEST,
        )

    def test_incomplete_body_at_eof(self):
        self.assert_raw_status(
            b"POST /api/tasks HTTP/1.1\r\nContent-Length: 20\r\n\r\n{",
            HTTPStatus.BAD_REQUEST, shutdown_write=True,
        )

    def test_delete_has_no_body_on_wire(self):
        status, _, _ = self.call("POST", TASKS_PATH, {"title": "socket contract"})
        self.assertEqual(status, HTTPStatus.CREATED)
        wire = self.raw_response(
            b"DELETE /api/tasks/1 HTTP/1.1\r\nHost: localhost\r\n\r\n",
        )
        self.assertEqual(wire, (
            b"HTTP/1.1 204 No Content\r\n"
            b"Content-Type: application/json\r\nContent-Length: 0\r\n"
            b"Connection: close\r\nCache-Control: no-store\r\n"
            b"X-Content-Type-Options: nosniff\r\n\r\n"
        ))

    def test_length_required_response_on_wire(self):
        wire = self.raw_response(b"POST /api/tasks HTTP/1.1\r\nHost: localhost\r\n\r\n")
        header, body = wire.split(b"\r\n\r\n", 1)
        self.assertEqual(header.split(b"\r\n")[0], b"HTTP/1.1 411 Length Required")
        self.assertEqual(body, b'{"error":"content-length required"}')
        headers = dict(line.decode("ascii").split(": ", 1)
                       for line in header.split(b"\r\n")[1:])
        self.assert_response_metadata(body, headers)

    def test_response_headers(self):
        cases = (
            (HEALTH_PATH, JSON_MIME),
            (TASKS_PATH, JSON_MIME),
            ("/", "text/html; charset=utf-8"),
        )
        for path, mime in cases:
            with self.subTest(path=path):
                status, body, headers = self.call("GET", path)
                self.assertEqual(status, HTTPStatus.OK)
                self.assert_response_metadata(body, headers, mime)

    def test_create_and_delete_response_metadata(self):
        title = "Învață 🥧 sockets"
        status, body, headers = self.call("POST", TASKS_PATH, {"title": title})
        self.assertEqual(status, HTTPStatus.CREATED)
        self.assertEqual(json.loads(body), {"id": 1, "title": title})
        self.assert_response_metadata(body, headers)
        status, body, headers = self.call("DELETE", TASKS_PATH + "/1")
        self.assertEqual(status, HTTPStatus.NO_CONTENT)
        self.assertEqual(body, b"")
        self.assert_response_metadata(body, headers, EMPTY_JSON_MIME)

    def test_invalid_delete_ids(self):
        for task_id in INVALID_TASK_IDS:
            with self.subTest(task_id=task_id):
                status, body, headers = self.call("DELETE", TASKS_PATH + "/" + task_id)
                self.assertEqual(status, HTTPStatus.NOT_FOUND)
                self.assertEqual(body, b'{"error":"not found"}')
                self.assert_response_metadata(body, headers)

    def test_error_response_contract(self):
        cases = (
            ("POST", TASKS_PATH, {"title": ""}, HTTPStatus.BAD_REQUEST,
             b'{"error":"invalid request"}'),
            ("POST", HEALTH_PATH, {}, HTTPStatus.NOT_FOUND,
             b'{"error":"not found"}'),
            ("GET", TASKS_PATH + "/1", None, HTTPStatus.NOT_FOUND,
             b'{"error":"not found"}'),
            ("PUT", TASKS_PATH, None, HTTPStatus.METHOD_NOT_ALLOWED,
             b'{"error":"method not allowed"}'),
        )
        for method, path, data, expected_status, expected_body in cases:
            with self.subTest(method=method, path=path, status=expected_status):
                status, body, headers = self.call(method, path, data)
                self.assertEqual(status, expected_status)
                self.assertEqual(body, expected_body)
                self.assert_response_metadata(body, headers)

    def test_oversized_length_response_metadata(self):
        # Rejecting the declared length must not require reading a request body.
        status, body, headers = self.call(
            "POST", TASKS_PATH, headers={"Content-Length": str(OVERSIZED_BODY_BYTES)},
        )
        self.assertEqual(status, HTTPStatus.REQUEST_ENTITY_TOO_LARGE)
        self.assertEqual(body, b'{"error":"request too large"}')
        self.assert_response_metadata(body, headers)

    def test_dashboard(self):
        status, body, headers = self.call("GET", "/")
        self.assertEqual(status, 200)
        self.assertIn(b"CServe Pi", body)
        self.assertIn("text/html", headers["Content-Type"])

    def test_empty_list(self):
        status, body, _ = self.call("GET", "/api/tasks")
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body), {"tasks": []})

    def test_capacity_and_reused_slot_order(self):
        expected = []
        for task_id in range(1, TASK_CAPACITY + 1):
            title = f"task {task_id}"
            status, body, _ = self.call("POST", TASKS_PATH, {"title": title})
            self.assertEqual(status, HTTPStatus.CREATED)
            task = {"id": task_id, "title": title}
            self.assertEqual(json.loads(body), task)
            expected.append(task)
        status, body, headers = self.call("POST", TASKS_PATH, {"title": "full"})
        self.assertEqual(status, HTTPStatus.INSUFFICIENT_STORAGE)
        self.assertEqual(body, b'{"error":"task limit reached"}')
        self.assert_response_metadata(body, headers)
        self.assertEqual(json.loads(self.call("GET", TASKS_PATH)[1]), {"tasks": expected})
        self.assertEqual(self.call("DELETE", TASKS_PATH + "/2")[:2], (HTTPStatus.NO_CONTENT, b""))
        status, body, _ = self.call("POST", TASKS_PATH, {"title": "replacement"})
        self.assertEqual(status, HTTPStatus.CREATED)
        replacement = {"id": TASK_CAPACITY + 1, "title": "replacement"}
        self.assertEqual(json.loads(body), replacement)
        expected[1] = replacement
        self.assertEqual(json.loads(self.call("GET", TASKS_PATH)[1]), {"tasks": expected})
        self.assertEqual(self.call("DELETE", TASKS_PATH + "/2")[0], HTTPStatus.NOT_FOUND)

    def test_create_read_delete(self):
        status, body, _ = self.call("POST", "/api/tasks", {"title": "Învață C & sockets"})
        self.assertEqual(status, 201)
        task = json.loads(body)
        self.assertEqual(task, {"id": 1, "title": "Învață C & sockets"})
        status, body, _ = self.call("GET", "/api/tasks")
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body), {"tasks": [task]})
        status, body, _ = self.call("DELETE", "/api/tasks/1")
        self.assertEqual((status, body), (204, b""))
        self.assertEqual(json.loads(self.call("GET", "/api/tasks")[1]), {"tasks": []})

    def test_escaping_json(self):
        title = 'A "quote" and a \\ backslash'
        status, body, _ = self.call("POST", "/api/tasks", {"title": title})
        self.assertEqual(status, 201)
        self.assertEqual(json.loads(body)["title"], title)

    def test_unicode_escape(self):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=2)
        try:
            title = "Învăț 🥧 sockets"
            encoded = json.dumps({"title": title}).encode("ascii")
            conn.request("POST", "/api/tasks", body=encoded,
                         headers={"Content-Type": "application/json"})
            response = conn.getresponse()
            self.assertEqual(response.status, 201)
            self.assertEqual(json.loads(response.read())["title"], title)
        finally:
            conn.close()

    def test_invalid_task(self):
        for bad in ({"title": ""}, {"title": " "}, {"title": 33}, {"title": "x" * 200}, {"x": "hello"}):
            with self.subTest(bad=bad):
                status, body, _ = self.call("POST", "/api/tasks", bad)
                self.assertEqual(status, 400)
                self.assertIn("error", json.loads(body))

    def post_json_bytes(self, body, status):
        header = (b"POST /api/tasks HTTP/1.1\r\nContent-Length: "
                  + str(len(body)).encode("ascii") + b"\r\n\r\n")
        return self.assert_raw_status(header + body, status)

    def test_title_ascii_byte_limits(self):
        title = "x" * TITLE_LIMIT_BYTES
        status, body, _ = self.call("POST", TASKS_PATH, {"title": title})
        self.assertEqual(status, HTTPStatus.CREATED)
        self.assertEqual(json.loads(body), {"id": 1, "title": title})
        status, _, _ = self.call("POST", TASKS_PATH, {"title": title + "x"})
        self.assertEqual(status, HTTPStatus.BAD_REQUEST)

    def test_title_unicode_byte_limits(self):
        title = "ă" * 60  # 120 UTF-8 bytes, rather than 120 characters.
        self.assertEqual(len(title.encode("utf-8")), TITLE_LIMIT_BYTES)
        status, body, _ = self.call("POST", TASKS_PATH, {"title": title})
        self.assertEqual(status, HTTPStatus.CREATED)
        self.assertEqual(json.loads(body), {"id": 1, "title": title})
        status, _, _ = self.call("POST", TASKS_PATH, {"title": title + "ă"})
        self.assertEqual(status, HTTPStatus.BAD_REQUEST)

    def test_surrogate_escape_validation(self):
        invalid = (b'{"title":"\\uD800"}', b'{"title":"\\uDC00"}',
                   b'{"title":"\\uD800\\uD800"}', b'{"title":"\\uD800\\u0041"}')
        for body in invalid:
            with self.subTest(body=body):
                self.post_json_bytes(body, HTTPStatus.BAD_REQUEST)
        body = self.post_json_bytes(b'{"title":"\\uD83D\\uDE42"}', HTTPStatus.CREATED)
        self.assertEqual(json.loads(body), {"id": 1, "title": "🙂"})

    def test_control_escapes_rejected(self):
        for escape in (b"\\n", b"\\t", b"\\u0000", b"\\u001f"):
            with self.subTest(escape=escape):
                self.post_json_bytes(b'{"title":"a' + escape + b'b"}',
                                     HTTPStatus.BAD_REQUEST)

    def test_extended_or_incomplete_json_rejected(self):
        invalid = (b'{"title":"x","other":1}', b'{"title":"x"} trailing',
                   b'{"title":"x","title":"y"}', b'{}', b'{"title":',
                   b'{"title":"x', b'{"title":"x"', b'{"title":"\\')
        for body in invalid:
            with self.subTest(body=body):
                self.post_json_bytes(body, HTTPStatus.BAD_REQUEST)

    def test_maximum_escaped_title_roundtrip(self):
        title = '"\\' * 60
        self.assertEqual(len(title.encode("utf-8")), TITLE_LIMIT_BYTES)
        status, body, headers = self.call("POST", TASKS_PATH, {"title": title})
        self.assertEqual(status, HTTPStatus.CREATED)
        task = {"id": 1, "title": title}
        self.assertEqual(json.loads(body), task)
        self.assert_response_metadata(body, headers)
        self.assertEqual(json.loads(self.call("GET", TASKS_PATH)[1]), {"tasks": [task]})

    def test_task_route_boundaries(self):
        paths = ("/api/tasks/1/extra", "/api/tasksX/1", "/api/tasks/", "/api/tasks?x=1")
        for path in paths:
            with self.subTest(path=path):
                self.assertEqual(self.call("DELETE", path)[0], HTTPStatus.NOT_FOUND)
                self.assertEqual(self.call("GET", path)[0], HTTPStatus.NOT_FOUND)

    def test_not_found_and_method(self):
        self.assertEqual(self.call("GET", "/nothing")[0], 404)
        self.assertEqual(self.call("DELETE", "/api/tasks/999")[0], 404)
        self.assertEqual(self.call("PUT", "/api/tasks")[0], 405)

    def test_reject_large_body(self):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=2)
        try:
            conn.request("POST", "/api/tasks", body="x" * 5000,
                         headers={"Content-Type": "application/json"})
            self.assertEqual(conn.getresponse().status, 413)
        finally:
            conn.close()


if __name__ == "__main__":
    unittest.main()
