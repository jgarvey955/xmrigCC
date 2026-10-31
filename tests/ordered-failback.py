#!/usr/bin/env python3
"""Verify ordered failover and recovery across three upstreams."""

import argparse
import json
import pathlib
import signal
import socket
import socketserver
import subprocess
import tempfile
import threading
import time


class PoolState:
    def __init__(self, index):
        self.index = index
        self.ready = True
        self.logins = 0
        self.connections = set()
        self.errors = []
        self.lock = threading.Lock()

    def available(self):
        with self.lock:
            return self.ready

    def register(self, connection):
        with self.lock:
            if not self.ready:
                return False
            self.connections.add(connection)
            return True

    def unregister(self, connection):
        with self.lock:
            self.connections.discard(connection)

    def login(self):
        with self.lock:
            self.logins += 1
            return self.logins

    def set_ready(self, ready):
        with self.lock:
            self.ready = ready
            connections = list(self.connections) if not ready else []

        for connection in connections:
            try:
                connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                connection.close()
            except OSError:
                pass

    def connection_count(self):
        with self.lock:
            return len(self.connections)


class PoolServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def start_pool(index):
    state = PoolState(index)
    blob = '10' + '00' * 75

    class Handler(socketserver.StreamRequestHandler):
        def handle(self):
            if not state.register(self.request):
                return

            try:
                for line in self.rfile:
                    if not state.available():
                        return

                    request = json.loads(line)
                    if request.get('method') == 'login':
                        login = state.login()
                        result = {
                            'id': f'session-{index}-{login}',
                            'extensions': ['algo'],
                            'job': {
                                'job_id': f'job-{index}-{login}',
                                'algo': 'rx/0',
                                'blob': blob,
                                'target': 'ffffffff',
                                'height': 100 + index,
                                'seed_hash': '00' * 32,
                            },
                        }
                    else:
                        result = {'status': 'KEEPALIVED'}

                    response = {'id': request.get('id'), 'result': result, 'error': None}
                    self.wfile.write(json.dumps(response).encode() + b'\n')
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass
            except Exception as exc:
                with state.lock:
                    state.errors.append(repr(exc))
            finally:
                state.unregister(self.request)

    server = PoolServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return state, server


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', required=True, type=pathlib.Path)
    parser.add_argument('--kind', choices=('miner', 'proxy'), required=True)
    args = parser.parse_args()

    root = pathlib.Path(tempfile.mkdtemp(prefix=f'ordered-failback-{args.kind}-'))
    pools = [start_pool(index) for index in range(3)]
    states = [item[0] for item in pools]
    servers = [item[1] for item in pools]
    ports = [server.server_address[1] for server in servers]
    events = root / 'events.log'
    stdout = root / 'stdout.log'

    upstreams = [
        {'url': f'127.0.0.1:{port}', 'algo': 'rx/0', 'user': 'test', 'pass': 'x'}
        for port in ports
    ]
    config = {
        'autosave': False,
        'background': False,
        'colors': False,
        'donate-level': 0,
        'log-file': str(events),
        'pools': upstreams,
        'retries': 1,
        'retry-pause': 1,
        'verbose': True,
        'watch': False,
    }
    command = [str(args.binary.resolve()), '-c', str(root / 'config.json')]

    if args.kind == 'miner':
        config.update({
            'cc-client': {'enabled': False},
            'cpu': {'enabled': True, 'huge-pages': False, 'rx/0': [-1]},
            'cuda': False,
            'dmi': False,
            'opencl': False,
            'randomx': {'mode': 'light', 'init': 1, 'rdmsr': False, 'wrmsr': False, 'numa': False},
        })
        command.append('--daemonized')
    else:
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            bind_port = listener.getsockname()[1]
        config.update({
            'bind': [{'host': '127.0.0.1', 'port': bind_port, 'tls': False}],
            'donate-over-proxy': False,
            'http': {'enabled': False},
            'mode': 'nicehash',
            'tls': {'enabled': False},
        })

    (root / 'config.json').write_text(json.dumps(config))
    output = stdout.open('wb')
    process = subprocess.Popen(command, cwd=root, stdout=output, stderr=subprocess.STDOUT)
    downstream = None

    if args.kind == 'proxy':
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            assert process.poll() is None, f'proxy exited early: {stdout.read_text(errors="replace")}'
            try:
                downstream = socket.create_connection(('127.0.0.1', bind_port), timeout=1)
                break
            except OSError:
                time.sleep(0.05)
        assert downstream is not None, 'proxy listener did not start'
        login = {'id': 1, 'method': 'login', 'params': {
            'login': 'test-worker', 'pass': 'x', 'agent': 'failback-test', 'algo': ['rx/0']}}
        downstream.sendall(json.dumps(login).encode() + b'\n')

    def logs():
        return events.read_text(errors='replace') if events.exists() else ''

    def active_count(index):
        return logs().count(f'use pool 127.0.0.1:{ports[index]}')

    def wait_for(condition, description, timeout=20):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            errors = [error for state in states for error in state.errors]
            assert not errors, errors
            assert process.poll() is None, f'{args.kind} exited early: {stdout.read_text(errors="replace")}'
            if condition():
                return
            time.sleep(0.05)
        raise AssertionError(f'timed out waiting for {description}; logs: {logs()}; artifacts: {root}')

    try:
        wait_for(lambda: active_count(0) >= 1, 'primary activation')

        states[0].set_ready(False)
        wait_for(lambda: active_count(1) >= 1, 'first fallback activation')

        states[1].set_ready(False)
        wait_for(lambda: active_count(2) >= 1, 'second fallback activation')

        states[1].set_ready(True)
        wait_for(lambda: active_count(1) >= 2, 'intermediate failback')
        wait_for(lambda: states[2].connection_count() == 0, 'second fallback disconnect')

        states[0].set_ready(True)
        wait_for(lambda: active_count(0) >= 2, 'primary failback')
        wait_for(lambda: states[1].connection_count() == 0, 'intermediate fallback disconnect')

        assert active_count(1) == 2, logs()
        assert active_count(0) == 2, logs()
        print(f'PASS {args.kind}: primary -> fallback 1 -> fallback 2 -> fallback 1 -> primary')
    finally:
        if downstream is not None:
            downstream.close()
        if process.poll() is None:
            process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        output.close()
        for server in servers:
            server.shutdown()
            server.server_close()
        print(f'Failback test artifacts: {root}')


if __name__ == '__main__':
    main()
