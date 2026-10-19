#!/usr/bin/env python3
"""Verify that asynchronous CC status publishing keeps its configured cadence."""
import argparse
import http.server
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import time


class StatusServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address):
        super().__init__(address, StatusHandler)
        self.condition = threading.Condition()
        self.status_times = []


class StatusHandler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        self.rfile.read(length)
        if self.path.startswith("/client/setClientStatus?clientId=cadence-test"):
            with self.server.condition:
                self.server.status_times.append(time.monotonic())
                self.server.condition.notify_all()
            body = b"{}"
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        self.send_error(404)

    def log_message(self, *_args):
        pass


def run(binary):
    with StatusServer(("127.0.0.1", 0)) as server, tempfile.TemporaryDirectory(prefix="xmrig-cc-cadence-") as directory:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()

        root = Path(directory)
        config = {
            "autosave": False,
            "watch": False,
            "background": False,
            "colors": False,
            "donate-level": 0,
            "discord": {"enabled": False},
            "cpu": {"enabled": False},
            "opencl": False,
            "cuda": False,
            "dmi": False,
            "http": {"enabled": False},
            "pools": [{"url": "127.0.0.1:9", "user": "cadence-test", "pass": "x"}],
            "cc-client": {
                "enabled": True,
                "use-remote-logging": False,
                "upload-config-on-start": False,
                "servers": [{
                    "url": f"127.0.0.1:{server.server_address[1]}",
                    "access-token": "cadence-test",
                    "http-proxy": "",
                    "socks-proxy": "",
                    "use-tls": False,
                }],
                "worker-id": "cadence-test",
                "reboot-cmd": None,
                "update-interval-s": 2,
                "retries-to-failover": 5,
            },
        }
        path = root / "config.json"
        path.write_text(json.dumps(config))

        proc = subprocess.Popen(
            [str(binary), "-c", str(path), "--daemonized"],
            cwd=root,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        output = []

        def drain_logs():
            for line in iter(proc.stdout.readline, b""):
                output.append(line.decode(errors="replace"))
                del output[:-40]

        reader = threading.Thread(target=drain_logs, daemon=True)
        reader.start()
        try:
            with server.condition:
                deadline = time.monotonic() + 12
                while not server.status_times and time.monotonic() < deadline:
                    server.condition.wait(deadline - time.monotonic())
                assert server.status_times, f"no CC status received; log={output}"

                deadline = time.monotonic() + 6.5
                while len(server.status_times) < 3 and time.monotonic() < deadline:
                    server.condition.wait(deadline - time.monotonic())

                timestamps = list(server.status_times)

            assert proc.poll() is None, f"miner stopped unexpectedly; log={output}"
            assert len(timestamps) >= 3, (
                "CC publisher skipped alternating timer ticks: "
                f"received {len(timestamps)} reports after the first within 6.5 seconds; log={output}"
            )
            intervals = [timestamps[i] - timestamps[i - 1] for i in range(1, 3)]
            assert max(intervals) < 3.25, f"CC report cadence doubled: intervals={intervals}"
            print(f"PASS CC reporting cadence: intervals={intervals}", flush=True)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
            reader.join(timeout=2)
            proc.stdout.close()
            server.shutdown()
            thread.join()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=lambda value: Path(value).resolve())
    args = parser.parse_args()
    run(args.binary)

