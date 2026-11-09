"""Black-box integration tests of the real C server using only Python stdlib."""
import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "build" / "cserve"


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

    def test_dashboard(self):
        status, body, headers = self.call("GET", "/")
        self.assertEqual(status, 200)
        self.assertIn(b"CServe Pi", body)
        self.assertIn("text/html", headers["Content-Type"])

    def test_empty_list(self):
        status, body, _ = self.call("GET", "/api/tasks")
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body), {"tasks": []})

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
