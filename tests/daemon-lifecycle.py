#!/usr/bin/env python3
"""Check POSIX daemon console startup and signal forwarding without mining."""
import argparse
import errno
import os
from pathlib import Path
import pty
import select
import signal
import subprocess
import sys
import tempfile
import time


MINER = r'''
import os
from pathlib import Path
import signal
import subprocess
import sys
import termios
import time

Path("miner.pid").write_text(str(os.getpid()))
assert sys.argv[1:] == ["argument with spaces", "--daemonized"], sys.argv
if os.isatty(0):
    # Setting terminal modes, like libuv's console, stops a background group.
    settings = termios.tcgetattr(0)
    termios.tcsetattr(0, termios.TCSANOW, settings)
    print("CONSOLE READY", flush=True)
    sys.exit(0 if sys.stdin.readline().strip() == "q" else 1)

term_exit_code = Path("term-exit-code")
if term_exit_code.exists():
    exit_code = int(term_exit_code.read_text())
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(exit_code))
    print("NONZERO TERM READY", flush=True)
    while True:
        time.sleep(1)

signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
if Path("stop-miner").exists():
    print("MINER STOPPING", flush=True)
    os.kill(os.getpid(), signal.SIGSTOP)
else:
    child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
    Path("descendant.pid").write_text(str(child.pid))

    def terminate(*_):
        child.wait(timeout=3)
        sys.exit(0)

    signal.signal(signal.SIGTERM, terminate)
    print("HEADLESS READY", flush=True)
while True:
    time.sleep(1)
'''


def read_until(fd, marker, alive, timeout=5):
    output = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if select.select([fd], [], [], 0.05)[0]:
            try:
                data = os.read(fd, 65536)
            except OSError as error:
                if error.errno != errno.EIO:
                    raise
                break
            if not data:
                break
            output.extend(data)
            if marker in output:
                return
        if not alive():
            break
    raise AssertionError(f"missing {marker!r}; output={output.decode(errors='replace')}")


def kill_fixture(root):
    for name in ("miner.pid", "descendant.pid"):
        path = root / name
        if path.exists():
            try:
                os.kill(int(path.read_text()), signal.SIGKILL)
            except ProcessLookupError:
                pass


def console_test(root, interrupt=False):
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(root)
        os.execv(str(root / "xmrigDaemon"), ["xmrigDaemon", "argument with spaces"])

    result = None

    def alive():
        nonlocal result
        if result is None:
            waited, status = os.waitpid(pid, os.WNOHANG)
            if waited:
                result = status
        return result is None

    try:
        read_until(fd, b"CONSOLE READY", alive)
        os.write(fd, b"\x03" if interrupt else b"q\n")
        deadline = time.monotonic() + 5
        while alive() and time.monotonic() < deadline:
            time.sleep(0.02)
        assert result is not None, "daemon did not exit after console command"
        expected = 128 + signal.SIGINT if interrupt else 0
        assert os.waitstatus_to_exitcode(result) == expected, result
    finally:
        kill_fixture(root)
        if alive():
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
        os.close(fd)


def headless_test(root, stopped=False):
    if stopped:
        (root / "stop-miner").touch()
    proc = subprocess.Popen(
        [str(root / "xmrigDaemon"), "argument with spaces"],
        cwd=root,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    try:
        read_until(proc.stdout.fileno(), b"MINER STOPPING" if stopped else b"HEADLESS READY",
                   lambda: proc.poll() is None)
        if stopped:
            # Give the child time to enter its stopped state before terminating.
            time.sleep(0.1)
        proc.terminate()
        assert proc.wait(timeout=5) == 0, "daemon did not shut down cleanly"
        for name in ("miner.pid", "descendant.pid"):
            path = root / name
            if path.exists():
                try:
                    os.kill(int(path.read_text()), 0)
                except ProcessLookupError:
                    continue
                raise AssertionError(f"{name} survived daemon shutdown")
    finally:
        kill_fixture(root)
        if proc.poll() is None:
            proc.kill()
        proc.wait()
        proc.stdout.close()


def nonzero_term_test(root, exit_code):
    (root / "term-exit-code").write_text(str(exit_code))
    proc = subprocess.Popen(
        [str(root / "xmrigDaemon"), "argument with spaces"],
        cwd=root,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    try:
        read_until(proc.stdout.fileno(), b"NONZERO TERM READY", lambda: proc.poll() is None)
        proc.terminate()
        assert proc.wait(timeout=5) == 0, (
            f"daemon respawned miner after shutdown exit {exit_code}"
        )
    finally:
        kill_fixture(root)
        if proc.poll() is None:
            proc.kill()
        proc.wait()
        proc.stdout.close()


def run(binary):
    for name, test in (
        ("terminal startup and quit", lambda root: console_test(root)),
        ("terminal Ctrl+C", lambda root: console_test(root, interrupt=True)),
        ("headless process group shutdown", lambda root: headless_test(root)),
        ("stopped miner shutdown", lambda root: headless_test(root, stopped=True)),
        ("shutdown after miner exit 1", lambda root: nonzero_term_test(root, 1)),
        ("shutdown after miner restart exit", lambda root: nonzero_term_test(root, 75)),
    ):
        with tempfile.TemporaryDirectory(prefix="xmrig-daemon-") as directory:
            root = Path(directory)
            (root / "xmrigDaemon").symlink_to(binary)
            miner = root / "xmrigMiner"
            miner.write_text(f"#!{sys.executable}\n" + MINER)
            miner.chmod(0o755)
            test(root)
            print(f"PASS {name}", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=lambda value: Path(value).resolve())
    args = parser.parse_args()
    run(args.binary)
