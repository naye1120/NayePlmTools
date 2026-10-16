#!/usr/bin/env python3
"""Compile the real HTTP handler with fault hooks and stubbed PS5 services.

All artifacts and storage fixtures live in a temporary directory. The network
soak uses real libmicrohttpd over loopback; no console or external service runs.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import http.client
import json
import os
from pathlib import Path
import shlex
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def run(args):
    subprocess.run(args, cwd=ROOT, check=True)


def eventually(predicate, message, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.02)
    raise AssertionError(message)


def lifecycle_tests(port, storage, server):
    payloads = storage / "payloads"

    def temporary_uploads():
        return list(payloads.glob(".upload-*")) + list(payloads.glob("*.tmp"))

    def partial_upload(filename="cancelled.elf", size=100000):
        sock = socket.create_connection(("127.0.0.1", port), timeout=10)
        sock.sendall(f"POST /manage:upload?filename={filename} HTTP/1.1\r\n"
                     f"Host: localhost\r\nX-PLDMGR-Request: 1\r\nContent-Length: {size}\r\n\r\n".encode() + b"first chunk")
        return sock

    # Abort after the daemon has opened its file, not before accepting the socket.
    for _ in range(8):
        sock = partial_upload()
        try:
            eventually(lambda: bool(temporary_uploads()), "Upload never opened a temporary file")
        finally:
            sock.close()
        eventually(lambda: not temporary_uploads(), "Disconnected upload left a temporary file")
    assert not (payloads / "cancelled.elf").exists()

    # Incomplete config/source bodies must be released without being saved.
    fd_directory = Path(f"/proc/{server.pid}/fd")
    baseline_fds = len(list(fd_directory.iterdir())) if fd_directory.exists() else None
    for route in ["/set_config", "/sources_set"] * 8:
        with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
            sock.sendall(f"POST {route} HTTP/1.1\r\nHost: localhost\r\nX-PLDMGR-Request: 1\r\n"
                         "Content-Length: 100000\r\n\r\n".encode() + b'{"incomplete":')
    if baseline_fds is not None:
        eventually(lambda: len(list(fd_directory.iterdir())) <= baseline_fds,
                   "Aborted requests leaked file descriptors")

    # Two in-flight requests for the same destination must have separate files.
    first = partial_upload("same.elf")
    second = None
    try:
        eventually(lambda: len(temporary_uploads()) == 1, "First upload did not open")
        second = partial_upload("same.elf")
        eventually(lambda: len(temporary_uploads()) == 2, "Same-name uploads shared a temporary file")
        first.close()
        eventually(lambda: len(temporary_uploads()) == 1, "Cancelling one upload affected the other")
    finally:
        first.close()
        if second:
            second.close()
    eventually(lambda: not temporary_uploads(), "Concurrent upload cleanup failed")

    # A normal import consumes the file; a failed import must still remove it.
    for filename, mode, status in [("success.elf", "new", 200), ("failed.elf", "fail_test", 500)]:
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
        try:
            connection.request("POST", f"/manage:upload?filename={filename}&mode={mode}", b"test payload", headers={"X-PLDMGR-Request": "1"})
            response = connection.getresponse()
            data = response.read()
            assert response.status == status, (response.status, data)
        finally:
            connection.close()
        eventually(lambda: not temporary_uploads(), "Completed upload left its temporary file")
    assert (payloads / "success.elf").read_bytes() == b"test payload"
    assert not (payloads / "failed.elf").exists()

    # Real chunked bodies exercise the unknown-length path, not just mock callbacks.
    for route in ["/set_config", "/sources_set", "/manage:upload?filename=chunked.elf"]:
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
        try:
            connection.request("POST", route, iter([b"{", b'"value":', b"true}"]), encode_chunked=True, headers={"X-PLDMGR-Request": "1"})
            response = connection.getresponse()
            assert response.status == 200, (response.status, response.read())
            response.read()
        finally:
            connection.close()
    eventually(lambda: not temporary_uploads(), "Chunked upload left a temporary file")
    assert (payloads / "chunked.elf").read_bytes() == b'{"value":true}'

    for route, limit in [("/set_config", 1024 * 1024), ("/sources_set", 1024 * 1024),
                         ("/manage:upload?filename=oversized.elf", 512 * 1024 * 1024)]:
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
        try:
            connection.putrequest("POST", route)
            connection.putheader("X-PLDMGR-Request", "1")
            connection.putheader("Content-Length", str(limit + 1))
            connection.endheaders()
            response = connection.getresponse()
            data = response.read()
            assert response.status == 413, (response.status, data)
            json.loads(data)
        finally:
            connection.close()
    assert not (payloads / "oversized.elf").exists()
    eventually(lambda: not temporary_uploads(), "Rejected oversized upload created a file")

    # Check large-list error responses using the actual HTTP transport and JSON parser.
    autoload = storage / "autoload.txt"
    try:
        # Exercise the production encoder through both real HTTP list routes.
        # Newlines are list separators; other controls remain string content.
        sample = 'Zażółć Україна 日本語 😀 "quoted" \\path\t\b\f\x01'
        autoload.write_text(sample, encoding="utf-8")
        for route, key in [("/get_config", "AUTOLOAD_LIST")]:
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
            try:
                connection.request("GET", route)
                response = connection.getresponse()
                data = json.loads(response.read())
                assert response.status == 200, (response.status, data)
                assert data[key] == sample, data
            finally:
                connection.close()
        autoload.write_bytes(b"\x01" * 4095)
        for route, key in [("/get_config", "AUTOLOAD_LIST")]:
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
            try:
                connection.request("GET", route)
                response = connection.getresponse()
                data = json.loads(response.read())
                assert response.status == 200 and data[key] == "\x01" * 4095, data
            finally:
                connection.close()
        for size in [4095, 4096, 20000]:
            autoload.write_text("a" * size)
            for route in ["/get_config"]:
                connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
                try:
                    connection.request("GET", route)
                    response = connection.getresponse()
                    data = json.loads(response.read())
                    assert response.status == (200 if size == 4095 else 500), (response.status, data)
                    if response.status == 200:
                        assert data["AUTOLOAD_LIST" if route == "/get_config" else "list"] == "a" * size
                    else:
                        assert "error" in data
                finally:
                    connection.close()
    finally:
        autoload.unlink(missing_ok=True)

    # Daemon stop must clean requests whose clients remain connected.
    pending = [partial_upload("shutdown.elf")]
    eventually(lambda: bool(temporary_uploads()), "Shutdown test upload did not open")
    for route in ["/set_config", "/sources_set"]:
        sock = socket.create_connection(("127.0.0.1", port), timeout=10)
        sock.sendall(f"POST {route} HTTP/1.1\r\nHost: localhost\r\nX-PLDMGR-Request: 1\r\n"
                     "Content-Length: 100000\r\n\r\n".encode() + b'{"incomplete":')
        pending.append(sock)
    return pending, temporary_uploads


def security_tests(port, storage, legacy=False):
    host = f"127.0.0.1:{port}"
    own = "http://" + host
    checks = 0

    def request(method, route, body=None, headers=None):
        nonlocal checks
        conn = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
        try:
            conn.request(method, route, body, headers or {})
            response = conn.getresponse()
            result = (response.status, dict(response.getheaders()), response.read())
            checks += 1
            return result
        finally:
            conn.close()

    def state():
        return json.loads(request("GET", "/__test_state")[2])

    if legacy:
        before = state()
        status, headers, _ = request("GET", "/abort", headers={"Origin": "http://evil.invalid"})
        assert status == 200 and state()["mutations"] > before["mutations"]
        before = state()
        status, headers, _ = request("POST", "/set_config", "{}", {"Origin": "http://evil.invalid"})
        assert status == 200 and state()["mutations"] > before["mutations"]
        status, headers, _ = request("OPTIONS", "/set_config", headers={"Origin": "http://evil.invalid", "Access-Control-Request-Method": "POST", "Access-Control-Request-Headers": "X-PLDMGR-Request"})
        assert status == 200 and headers.get("Access-Control-Allow-Origin") == "*"
        print("Previous implementation reproduced foreign GET/POST side effects and wildcard preflight")
        return

    routes = ["/abort", "/autoload_clear", "/process_kill?pid=123", "/repository_refresh",
              "/repository_cache_clear", "/repository_install?id=test", "/sources_add?url=http://example.invalid",
              "/sources_remove?index=1", "/usb_move_perform?path=/mnt/usb0/test.elf",
              "/manage:delete?slot=local/test", "/loadpayload:missing.elf",
              "/set_config", "/sources_set", "/manage:upload?filename=policy.elf", "/shutdown"]
    for route in routes:
        for method, headers, expected in [
            ("GET", {}, 405), ("GET", {"Origin": own}, 405),
            ("POST", {}, 403), ("POST", {"Origin": "http://evil.invalid"}, 403),
            ("POST", {"Origin": "http://evil.invalid", "X-PLDMGR-Request": "1"}, 403),
            ("POST", {"Origin": "null", "X-PLDMGR-Request": "1"}, 403),
            ("POST", {"Referer": "http://evil.invalid/page", "X-PLDMGR-Request": "1"}, 403),
            ("POST", {"X-PLDMGR-Request": "wrong"}, 403),
            ("POST", {"X-PLDMGR-Request": "1", "Sec-Fetch-Site": "cross-site"}, 403),
            ("PUT", {"Origin": own}, 405), ("HEAD", {"Origin": own}, 405),
        ]:
            before = state()
            status, response_headers, _ = request(method, route, "{}" if method in ["POST", "PUT"] else None, headers)
            assert status == expected, (method, route, status, expected)
            assert state() == before, (method, route, "rejection changed backend/autoload state")
            assert not list((storage / "payloads").glob(".upload-*")), route
            if headers.get("Origin") != own:
                assert "Access-Control-Allow-Origin" not in response_headers, response_headers
    for bad in [own + ".evil", own + "/page", own + " http://evil.invalid", "https://" + host, "file://"]:
        before = state()
        assert request("POST", "/abort", headers={"Origin": bad, "X-PLDMGR-Request": "1"})[0] == 403
        assert state() == before
    for route in ["/list_payloads", "/get_config", "/sources_list", "/history_list"]:
        before = state()
        status, headers, _ = request("GET", route, headers={"Origin": "http://evil.invalid"})
        assert status == 200 and "Access-Control-Allow-Origin" not in headers
        assert state() == before, "foreign reads activated browser/autoload state"
        assert request("POST", route, headers={"Origin": own})[0] == 405
    for origin, method, requested_headers, expected in [
        (own, "POST", "content-type, x-pldmgr-request", 200),
        (own, "POST", "X-PLDMGR-Request", 200), (own, "GET", "", 403),
        (own, "POST", "Authorization", 403), (own, "POST", "Content-Type,", 403),
        ("http://evil.invalid", "POST", "X-PLDMGR-Request", 403),
        ("null", "POST", "Content-Type", 403),
    ]:
        before = state()
        status, headers, _ = request("OPTIONS", "/set_config", headers={"Origin": origin, "Access-Control-Request-Method": method, "Access-Control-Request-Headers": requested_headers})
        assert status == expected and state() == before
        if expected == 200:
            assert headers.get("Access-Control-Allow-Origin") == own
            assert headers.get("Access-Control-Allow-Methods") == "POST"
    # Newly migrated action POSTs must wait for the full bounded body, and
    # disconnecting an incomplete action must not execute it.
    before = state()
    sock = socket.create_connection(("127.0.0.1", port), timeout=10)
    sock.sendall(b"POST /abort HTTP/1.1\r\nHost: localhost\r\nX-PLDMGR-Request: 1\r\nContent-Length: 100\r\n\r\n{}")
    eventually(lambda: state()["active"] > before["active"], "Action body was never accepted")
    assert state()["mutations"] == before["mutations"]
    sock.close()
    assert state()["mutations"] == before["mutations"]
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
    try:
        connection.putrequest("POST", "/abort")
        connection.putheader("X-PLDMGR-Request", "1")
        connection.putheader("Content-Length", str(1024 * 1024 + 1))
        connection.endheaders()
        response = connection.getresponse(); response.read()
        assert response.status == 413
    finally: connection.close()
    assert state()["mutations"] == before["mutations"]
    # Same-origin browser requests and beacons (JSON and text/plain), legacy
    # Referer-only browsers, and explicit originless LAN clients all work.
    for headers in [{"Origin": own}, {"Referer": own + "/index.html"},
                    {"X-PLDMGR-Request": "1"}, {"Origin": own, "Content-Type": "text/plain"},
                    {"Origin": own, "Content-Type": "application/json"}]:
        before = state()
        status, _, _ = request("POST", "/set_config", "{}", headers)
        assert status == 200 and state()["mutations"] == before["mutations"] + 1
    # Every action consumer reaches its handler with the explicit marker.
    for route in routes[:-1]:
        before = state()
        status, _, _ = request("POST", route, "{}", {"X-PLDMGR-Request": "1"})
        assert status == (400 if route.startswith("/loadpayload:") else 200), (route, status)
        assert state()["mutations"] == before["mutations"] + 1, route
    # OPTIONS and denied methods must never shut down the test daemon. A real
    # authorized shutdown is covered by unit routing; leave this server live.
    assert state()["running"] == 1
    print(f"HTTP mutation policy: {checks} transport/state checks passed")

def network_tests(binary, storage, security_only=False, legacy_security=False):
    server = subprocess.Popen([str(binary), "--serve"], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, text=True)
    try:
        line = server.stdout.readline().strip()
        if not line.startswith("PORT "):
            raise RuntimeError(f"Test HTTP server did not start: {line}")
        port = int(line.split()[1])
        security_tests(port, storage, legacy_security)
        if security_only:
            server.stdin.write("\n"); server.stdin.flush()
            output, _ = server.communicate(timeout=10); print(output.strip())
            assert server.returncode == 0
            return
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
        routes = ["/manage:check?filename=test.elf", "/history_list", "/update_check",
                  "/repository_payloads", "/sources_list", "/get_config", "/autoload_status", "/list_payloads"]
        try:
            for i in range(10000):
                connection.request("GET", routes[i % len(routes)])
                response = connection.getresponse()
                data = response.read()
                assert response.status == 200, (response.status, data)
                origins = [value for key, value in response.getheaders()
                           if key.lower() == "access-control-allow-origin"]
                assert origins == [], origins
                json.loads(data)
        finally:
            connection.close()
        def parallel_poll(index):
            route = ["/list_payloads", "/repository_payloads", "/sources_list", "/log"][index % 4]
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
            try:
                connection.request("GET", route)
                response = connection.getresponse()
                assert response.status == 200
                assert [value for key, value in response.getheaders()
                        if key.lower() == "access-control-allow-origin"] == []
                json.loads(response.read())
            finally:
                connection.close()
        with ThreadPoolExecutor(max_workers=8) as executor:
            list(executor.map(parallel_poll, range(64)))
        pending, temporary_uploads = lifecycle_tests(port, storage, server)
        server.stdin.write("\n")
        server.stdin.flush()
        try:
            output = server.communicate(timeout=15)[0]
        finally:
            for sock in pending:
                sock.close()
        print(output.strip())
        assert server.returncode == 0, "Unreleased responses or sanitizer failure during soak"
        assert not temporary_uploads(), "Daemon stop left an upload temporary file"
        print("HTTP integration: 10,000 real loopback requests passed")
        print("HTTP concurrency: 64 requests across 8 concurrent clients passed")
        print("HTTP lifecycle: disconnects, concurrent uploads, failed commits, and daemon stop passed")
    finally:
        if server.poll() is None:
            server.kill()
            server.communicate()


def config_network_tests(binary, storage):
    server = subprocess.Popen([str(binary), "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        line = server.stdout.readline().strip()
        assert line.startswith("PORT "), line
        port = int(line.split()[1])
        def request(method, route, body=None):
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
            try:
                connection.request(method, route, body.encode("utf-8") if isinstance(body, str) else body, {"X-PLDMGR-Request":"1"})
                response = connection.getresponse()
                return response.status, response.read()
            finally: connection.close()
        assert request("POST", "/set_config", '{"AUTO_BROWSER_OPEN":false}')[0] == 200
        status, body = request("GET", "/get_config")
        assert status == 200 and json.loads(body)["AUTO_BROWSER_OPEN"] is False
        for bad in ['{', '{"AUTOLOAD_ENABLED":false,"AUTOLOAD_DELAY":-1}', '{"FAVORITES":[1]}']:
            assert request("POST", "/set_config", bad)[0] == 400
        # A genuine filesystem failure must produce 500 and retain old config.
        old = (storage / "nayeplmtools_config.txt").read_bytes()
        (storage / "favorites.txt").mkdir()
        assert request("POST", "/set_config", '{"AUTO_BROWSER_OPEN":true,"FAVORITES":"local/x"}')[0] == 500
        assert (storage / "nayeplmtools_config.txt").read_bytes() == old
        (storage / "favorites.txt").rmdir()
        assert request("POST", "/set_config", '{"FAVORITES":"local/żółw","AUTOLOAD_DELAY":0}')[0] == 200
        status, body = request("GET", "/get_config")
        config = json.loads(body)
        assert status == 200 and config["FAVORITES"] == "local/żółw" and config["AUTOLOAD_DELAY"] == 0
        server.stdin.write("\n"); server.stdin.flush()
        output, _ = server.communicate(timeout=10)
        assert server.returncode == 0, output
        print(output.strip())
        print("HTTP real config: independent save/read, invalid input 400, I/O failure 500, retained settings and Unicode/zero passed")
    finally:
        if server.poll() is None: server.kill(); server.communicate()


def autoload_network_tests(binary, storage):
    server = subprocess.Popen([str(binary), "--serve"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        line = server.stdout.readline().strip()
        assert line.startswith("PORT "), line
        port = int(line.split()[1])
        def request(method="GET", route="/autoload_status", headers=None):
            connection = http.client.HTTPConnection("127.0.0.1",port,timeout=10)
            try:
                connection.request(method,route,headers=headers or {})
                response = connection.getresponse()
                body = response.read()
                assert response.status == 200,(response.status,body)
                return json.loads(body) if route == "/autoload_status" else body
            finally: connection.close()
        def state_is(wanted, headers=None):
            snapshot = request(headers=headers)
            assert snapshot["done"] == snapshot["successful"] + snapshot["failed"] + snapshot["skipped"]
            return snapshot["state"] == wanted
        eventually(lambda: state_is("waiting"), "worker did not enter frontend wait")
        time.sleep(0.1)
        assert state_is("waiting", {"Origin":"http://evil.invalid"})
        assert state_is("waiting"), "anonymous or foreign status read triggered the worker"
        marked = {"X-PLDMGR-Request":"1"}
        eventually(lambda: state_is("countdown",marked), "authorized frontend read did not start countdown")
        (storage / "autoload.txt").write_text("local/changed\n")
        snapshot = request()
        assert snapshot["list"] == "local/running" and snapshot["total"] == 1
        assert snapshot["entry_index"] == -1 and snapshot["entry_duration_ms"] == 0 and snapshot["entry_remaining_ms"] == 0
        request("POST","/autoload_clear",marked)
        assert state_is("countdown"), "reset erased active countdown"
        request("POST","/abort",marked)
        eventually(lambda: state_is("aborted"), "abort did not stop countdown")
        assert request()["remaining"] == -1
        request("POST","/autoload_clear",marked)
        snapshot = request()
        assert snapshot["state"] == "disabled" and snapshot["total"] == 0
        assert snapshot["entry_index"] == -1 and snapshot["entry_remaining_ms"] == 0
        server.stdin.write("\n");server.stdin.flush()
        output,_=server.communicate(timeout=10)
        assert server.returncode == 0,output
        print(output.strip())
        print("HTTP actual autoload: foreign/anonymous reads stay passive; authorized readiness, coherent sequence, active reset, abort and terminal clear passed")
    finally:
        if server.poll() is None: server.kill();server.communicate()


def log_network_tests(binary):
    server = subprocess.Popen([str(binary), '--serve'], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, text=True)
    clients = []
    try:
        line = server.stdout.readline()
        assert line.startswith('PORT '), line
        port = int(line.split()[1])
        def command(text):
            server.stdin.write(text + '\n'); server.stdin.flush()
            while True:
                line = server.stdout.readline()
                assert line, 'server exited before ACK'
                if line.strip() == 'ACK': return
        def connect(identity=None):
            conn = http.client.HTTPConnection('127.0.0.1', port, timeout=3)
            clients.append(conn)
            conn.request('GET', '/events', headers={} if identity is None else {'Last-Event-ID': str(identity)})
            return conn
        def response(conn):
            stream = conn.getresponse()
            assert stream.status == 200 and stream.getheader('Content-Type') == 'text/event-stream'
            return stream
        def read_event(stream):
            identity, data = None, []
            while True:
                line = stream.readline().decode()
                assert line, 'unexpected stream end'
                if line == '\n':
                    if data: return identity, '\n'.join(data)
                    continue
                if line.startswith('id: '): identity = int(line[4:])
                if line.startswith('data: '): data.append(line[6:].removesuffix('\n'))
        def expect(stream, first, end):
            for i in range(first, end):
                assert read_event(stream) == (i + 1, f'entry {i:03d} {i:0180d}')
        def expect_closed(stream):
            # MHD stop can close chunked transport before its final zero chunk.
            try: remainder = stream.read()
            except http.client.IncompleteRead as error: remainder = error.partial
            assert remainder == b''
        command('emit 0 80')
        a, b = connect(), connect()
        sa, sb = response(a), response(b)
        expect(sa, 0, 80); expect(sb, 0, 80)
        command('emit 80 20'); expect(sa, 80, 100); expect(sb, 80, 100)
        sb.close(); b.close()
        c = connect(95); sc = response(c); expect(sc, 95, 100)
        time.sleep(0.05)  # Readers now idle in a production five-second wait.
        started = time.monotonic(); command('restart')
        assert time.monotonic() - started < 1, 'idle SSE delayed daemon restart'
        expect_closed(sa); expect_closed(sc)
        d = connect(100)
        command('emit 100 1'); sd = response(d); expect(sd, 100, 101)
        time.sleep(0.05)
        started = time.monotonic(); server.stdin.write('stop\n'); server.stdin.flush()
        output, _ = server.communicate(timeout=3)
        assert server.returncode == 0 and time.monotonic() - started < 1, output
        expect_closed(sd)
        print(output.strip())
        print('HTTP actual SSE: >1 KiB catch-up, ordered bursts, two subscribers, disconnect/resume, idle restart and shutdown passed')
    finally:
        for client in clients: client.close()
        if server.poll() is None: server.kill(); server.communicate()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ownership-only", action="store_true",
                        help="Run basic ownership tests against the original source")
    parser.add_argument("--oom-only", action="store_true",
                        help="Reproduce OOM status/header bugs without deeper failure injection")
    parser.add_argument("--no-sanitize", action="store_true")
    parser.add_argument("--network-only", action="store_true")
    parser.add_argument("--security-only", action="store_true")
    parser.add_argument("--legacy-security", action="store_true")
    parser.add_argument("--serve-only", action="store_true")
    parser.add_argument("--config-only", action="store_true")
    parser.add_argument("--autoload-only", action="store_true")
    parser.add_argument("--logs-only", action="store_true")
    parser.add_argument("--source", default="src/http_server.c",
                        help="Alternate HTTP source snapshot for regression reproduction")
    args = parser.parse_args()
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1")
    os.environ.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    try:
        flags = shlex.split(subprocess.check_output(
            ["pkg-config", "--cflags", "--libs", "libmicrohttpd"], text=True))
    except (FileNotFoundError, subprocess.CalledProcessError):
        raise SystemExit("Install libmicrohttpd-dev and pkg-config, or use tests/Dockerfile.")
    compiler = shlex.split(os.environ.get("HOST_CC", "clang"))
    with tempfile.TemporaryDirectory(prefix="pldmgr-http-tests-") as temp:
        build = Path(temp)
        storage = build / "data"
        storage.mkdir()
        # Test-only header overlay: production path defaults never point here.
        # [CN-FORK] v0.6.0.176 数据目录在 fork 里改名为 /data/nayeplmtools（上游是 /data/pldmgr）。
        header = (ROOT / "include/pldmgr.h").read_text().replace("/data/pldmgr", str(storage))
        header = header.replace("/data/nayeplmtools", str(storage))
        (build / "pldmgr.h").write_text(header)
        for name, data in [("index_html", "<html>Test</html>"),
                           ("cache_appcache", "CACHE MANIFEST\n"),
                           ("favicon_svg", "<svg/>"), ("icon_png", "PNG")]:
            values = ",".join(str(byte) for byte in data.encode())
            (build / f"assets_{name}.h").write_text(
                f"static const unsigned char assets_{name}[] = {{{values}}};\n"
                f"static const unsigned int assets_{name}_len = sizeof(assets_{name});\n")
        common = compiler + ["-std=c11", "-D_DEFAULT_SOURCE", "-g", "-O1", "-Wall", "-Wextra",
                             "-I" + str(build), "-Iinclude",
                             # [CN-FORK] v0.6.0.177 桩文件要照 include/psaccounts/api.h 的
                             # 原型声明 psaccounts_*（-Isrc 让 "psaccounts/api.h" 能解析）。
                             "-Isrc",
                             # [CN-FORK] v0.6.0.176 fork 的 src/config.c 引 cJSON（取
                             # ARCHIVE_GLOBAL_CONFIG），上游不引 —— 补第三方头目录。
                             "-Isrc/psaccounts/third_party",
                             "-pthread"]
        if args.config_only or args.autoload_only:
            common += ["-DPLDMGR_TEST_REAL_CONFIG"]
        if args.autoload_only:
            common += ["-DPLDMGR_TEST_REAL_AUTOLOAD"]
        if args.logs_only:
            common += ["-DPLDMGR_TEST_REAL_LOGS"]
        if not args.no_sanitize:
            common += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        cflags = [flag for flag in flags if flag.startswith("-I") or flag.startswith("-D")]
        object_file = build / "http_server.o"
        run(common + cflags + ["-include", "tests/http_test_hooks.h", "-c",
                               args.source, "-o", str(object_file)])
        binary = build / "http_tests"
        extra_sources = ["src/config.c"] if args.config_only or args.autoload_only else []
        if args.logs_only:
            extra_sources += ["src/log_server.c"]
        if args.autoload_only:
            extra_sources += ["src/autoload.c"]
            (storage / "nayeplmtools_config.txt").write_text("AUTOLOAD_ENABLED=1\nAUTO_BROWSER_OPEN=1\nAUTOLOAD_DELAY=5\n")
            (storage / "autoload.txt").write_text("local/running\n")
        # [CN-FORK] v0.6.0.177 fork 自有路由的后端桩。缺了它链接不过（几十个
        # undefined reference），这套测试在 fork 上是**长期跑不了**的 —— 跑不了的
        # 测试和跑绿的测试长得一样，405 那批 bug 因此一直没人发现。见
        # tests/fork_backend_stubs.c 文件头。
        run(common + cflags + extra_sources + [str(object_file), "src/utils.c", "src/json_helpers.c", "tests/http_backend_stubs.c",
                               "tests/fork_backend_stubs.c",
                               "tests/http_responses_test.c",
                               # [CN-FORK] v0.6.0.176 fork 的 config.c/http_server.c 引 cJSON（上游不引），
                               # 生产构建经 Makefile 的 SRCS 链入，宿主测试这里补上实现。
                               "src/psaccounts/third_party/cJSON.c", "-o", str(binary)] + flags)
        selected = ["--ownership-only"] if args.ownership_only else (["--oom-only"] if args.oom_only else [])
        if args.autoload_only:
            autoload_network_tests(binary, storage); return
        if args.logs_only:
            log_network_tests(binary); return
        if args.config_only:
            config_network_tests(binary, storage); return
        if args.serve_only:
            run([str(binary), "--serve"]); return
        if not args.network_only and not args.security_only:
            run([str(binary)] + selected)
        if not selected:
            network_tests(binary, storage, args.security_only, args.legacy_security)


if __name__ == "__main__":
    main()
