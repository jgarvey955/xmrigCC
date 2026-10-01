#!/usr/bin/env python3
"""Check real miners' CC nonce reports against local Stratum assignments."""
import http.server
import json
from pathlib import Path
import queue
import socketserver
import subprocess
import sys
import tempfile
import threading
import time
from contextlib import contextmanager


@contextmanager
def serve(server_type, handler):
    server = server_type(('127.0.0.1', 0), handler)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield server.server_address[1]
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


def run_case(binary, root, algo):
    reports = queue.Queue()
    prefixes = {'worker-a': 0x00, 'worker-b': 0xfe}

    class CC(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            request = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            if self.path.startswith('/client/setClientStatus?'):
                reports.put(request['client_status'])
            body = b'{"control_command":{"command":"START"}}'
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    class Pool(socketserver.StreamRequestHandler):
        def handle(self):
            try:
                for line in self.rfile:
                    request = json.loads(line)
                    if request['method'] == 'login':
                        worker = request['params']['login']
                        blob = bytearray(140 if algo.startswith('rx/zecnero') else 76)
                        blob[0] = 4 if algo.startswith('rx/zecnero') else 16
                        # Reproduce a shared header byte that is not Zecnero's nonce.
                        blob[42] = 0x08
                        blob[111 if algo.startswith('rx/zecnero') else 42] = prefixes[worker]
                        result = {'id': worker, 'extensions': ['nicehash', 'algo'], 'job': {
                            'job_id': worker, 'algo': algo, 'blob': blob.hex(),
                            'target': 'ffffffff', 'seed_hash': '00' * 32,
                            'height': 100, 'proxy_mapper_id': 0, 'nonce_prefix': prefixes[worker]}}
                    else:
                        result = {'status': 'OK'}
                    self.wfile.write(json.dumps({'id': request['id'], 'result': result, 'error': None}).encode() + b'\n')
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                pass

    with serve(http.server.ThreadingHTTPServer, CC) as cc_port, \
            serve(socketserver.ThreadingTCPServer, Pool) as pool_port:
        processes = []
        try:
            for worker in prefixes:
                config = {
                    'autosave': False, 'watch': False, 'background': False, 'colors': False,
                    'donate-level': 0, 'dmi': False, 'http': {'enabled': False},
                    'discord': {'enabled': False}, 'opencl': False, 'cuda': False,
                    'cpu': {'enabled': True, 'huge-pages': False, 'rx': [-1]},
                    'randomx': {'mode': 'light', 'init': 1, 'rdmsr': False, 'wrmsr': False, 'numa': False},
                    'pools': [{'url': f'127.0.0.1:{pool_port}', 'user': worker, 'pass': 'x', 'algo': algo}],
                    'cc-client': {'enabled': True, 'url': f'127.0.0.1:{cc_port}', 'worker-id': worker,
                                  'access-token': 'local-test', 'use-tls': False, 'update-interval-s': 1,
                                  'upload-config-on-start': False, 'use-remote-logging': False}}
                path = root / (algo.replace('/', '-') + '-' + worker + '.json')
                path.write_text(json.dumps(config))
                with path.with_suffix('.log').open('wb') as log:
                    processes.append(subprocess.Popen([str(binary), '-c', str(path), '--daemonized'],
                        cwd=root, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT))
            pending = set(prefixes)
            deadline = time.monotonic() + 30
            while pending and time.monotonic() < deadline:
                assert all(proc.poll() is None for proc in processes), f'Miner exited; see {root}'
                try:
                    status = reports.get(timeout=0.2)
                except queue.Empty:
                    continue
                if not status.get('nonce_partitioned'):
                    continue
                worker = status['client_id']
                prefix = prefixes[worker]
                assert status['nonce_mapper_id'] == 0, status
                assert status['nonce_prefix'] == prefix, (
                    f'{algo} {worker}: expected 0x{prefix:02x}, got 0x{status["nonce_prefix"]:02x}')
                assert status['nonce_start'] == prefix << 24, status
                assert status['nonce_end'] == (prefix << 24) | 0xffffff, status
                pending.discard(worker)
            assert not pending, f'Missing CC reports from {pending}; see {root}'
        finally:
            for proc in processes:
                proc.terminate()
            for proc in processes:
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
    print(f'PASS {algo}: separate worker prefixes and nonce ranges on mapper 0', flush=True)


if __name__ == '__main__':
    root = Path(tempfile.mkdtemp(prefix='nonce-mapping-test-'))
    print(f'Test artifacts: {root}', flush=True)
    binary = Path(sys.argv[1]).resolve()
    for algo in ['rx/zecnero', 'rx/zecnero2', 'rx/0']:
        run_case(binary, root, algo)
