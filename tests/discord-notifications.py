#!/usr/bin/env python3
"""Exercise Discord notifications using a local mock pool and webhook only."""
import argparse
from contextlib import contextmanager
import http.server
import json
from pathlib import Path
import queue
import socket
import socketserver
import subprocess
import tempfile
import threading
import time


@contextmanager
def serve(cls, handler):
    server = cls(('127.0.0.1', 0), handler)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield server.server_address[1]
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


def run_case(binary, root, mode, coin, batch=False):
    messages = queue.Queue()
    original = {'job_id': 'first', 'algo': 'rx/0', 'blob': '10' + '00' * 75,
                'target': 'ffffffff', 'height': 6951, 'seed_hash': '00' * 32}
    replacement = dict(original, job_id='second', algo='rx/wow', height=6952)

    class Webhook(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_POST(self):
            payload = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            messages.put(payload['content'])
            self.send_response(204)
            self.send_header('Content-Length', '0')
            self.end_headers()

    class Pool(socketserver.StreamRequestHandler):
        def send(self, value):
            self.wfile.write(json.dumps(value).encode() + b'\n')
            self.wfile.flush()

        def handle(self):
            submissions = 0
            try:
                for line in self.rfile:
                    request = json.loads(line)
                    error = None
                    if request['method'] == 'login':
                        result = {'id': 'session', 'job': original, 'extensions': ['algo']}
                    elif request['method'] == 'submit':
                        submissions += 1
                        result = {'status': 'OK'}
                        if submissions == 1:
                            error = {'code': -1, 'message': 'test rejection'}
                        elif submissions == 2:
                            # Change jobs before acknowledging the previous job's share.
                            self.send({'method': 'job', 'params': replacement})
                            time.sleep(0.1)
                    else:
                        result = {'status': 'KEEPALIVED'}
                    self.send({'id': request['id'], 'result': result, 'error': error})
            except (ConnectionError, OSError):
                pass

    with serve(socketserver.ThreadingTCPServer, Pool) as pool_port, \
            serve(http.server.ThreadingHTTPServer, Webhook) as webhook_port:
        upstream = {'url': f'127.0.0.1:{pool_port}', 'user': 'test-worker', 'pass': 'x', 'coin': coin}
        config = {'autosave': False, 'watch': False, 'background': False, 'colors': False,
                  'donate-level': 0, 'pools': [upstream], 'http': {'enabled': False},
                  'tls': {'enabled': False}, 'discord': {
                      'enabled': True, 'webhook': f'http://127.0.0.1:{webhook_port}/test',
                      'notify-rejected': True, 'include-worker': False, 'include-totals': False,
                      'verbose': False, 'accepted-interval': 1 if batch else 0}}
        if mode == 'miner':
            config.update({'dmi': False, 'cc-client': {'enabled': False},
                           'randomx': {'mode': 'light', 'init': 1, 'rdmsr': False, 'wrmsr': False, 'numa': False},
                           'cpu': {'enabled': True, 'huge-pages': False, 'rx/0': [-1], 'rx/wow': [-1]},
                           'opencl': False, 'cuda': False})
        else:
            with socket.socket() as sock:
                sock.bind(('127.0.0.1', 0))
                listen = sock.getsockname()[1]
            config.update({'mode': mode, 'donate-over-proxy': False,
                           'bind': [{'host': '127.0.0.1', 'port': listen}]})
        label = f'{mode}-{coin or "unknown"}-{"batch" if batch else "single"}'
        path = root / (label + '.json')
        path.write_text(json.dumps(config))
        worker = stream = None
        with (root / (label + '.log')).open('wb') as log:
            command = [str(binary), '-c', str(path)]
            if mode == 'miner':
                command.append('--daemonized')
            proc = subprocess.Popen(command, cwd=root,
                                    stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
        try:
            if mode != 'miner':
                deadline = time.monotonic() + 10
                while worker is None:
                    assert proc.poll() is None, f'Proxy exited: {label}'
                    try:
                        worker = socket.create_connection(('127.0.0.1', listen), timeout=0.2)
                    except OSError:
                        assert time.monotonic() < deadline, 'Proxy did not listen'
                        time.sleep(0.05)
                worker.settimeout(10)
                stream = worker.makefile('rb')
                job = None

                def request(value):
                    nonlocal job
                    worker.sendall(json.dumps(value).encode() + b'\n')
                    while True:
                        reply = json.loads(stream.readline())
                        if reply.get('method') == 'job':
                            job = reply['params']
                        if reply.get('id') == value['id']:
                            return reply

                login = request({'id': 1, 'method': 'login', 'params': {
                    'login': 'worker', 'pass': 'x', 'agent': 'discord-test', 'algo': ['rx/0', 'rx/wow']}})['result']
                job = login['job']
                for seq in [2, 3, 4] if batch else [2, 3]:
                    if seq == 4:
                        time.sleep(1.1)
                    blob = bytes.fromhex(job['blob'])
                    reply = request({'id': seq, 'method': 'submit', 'params': {
                        'id': login['id'], 'job_id': job['job_id'],
                        'nonce': (bytes([seq, 0, 0]) + blob[42:43]).hex(),
                        'result': '00' * 24 + '0100000000000000'}})
                    assert bool(reply.get('error')) == (seq == 2), reply

            expected_coin = 'Salvium (SAL)' if coin else 'Unknown (set pool coin)'

            def notification():
                deadline = time.monotonic() + 60
                while time.monotonic() < deadline:
                    assert proc.poll() is None, f'Process exited: see {root / (label + ".log")}'
                    try:
                        return messages.get(timeout=0.2)
                    except queue.Empty:
                        pass
                raise AssertionError(f'No notification: see {root / (label + ".log")}')

            rejected = notification()
            assert rejected.startswith('Rejected block/share:'), rejected
            assert f'\nCoin: {expected_coin}\nAlgorithm: rx/0' in rejected, rejected
            accepted = notification()
            if batch:
                assert 'accepted block/share events in' in accepted, accepted
                assert f'\nLast coin: {expected_coin}\nLast algorithm: rx/wow' in accepted, accepted
            else:
                assert accepted.startswith('Accepted block/share:'), accepted
                assert f'\nCoin: {expected_coin}\nAlgorithm: rx/0' in accepted, accepted
            assert 'Worker:' not in accepted and 'Totals:' not in accepted, accepted
            (root / (label + '-message.txt')).write_text(accepted)
            print(f'PASS {label}: accepted/rejected metadata, job change before reply, verbosity off', flush=True)
        finally:
            if stream:
                stream.close()
            if worker:
                worker.close()
            proc.terminate()
            try:
                proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument('--proxy', type=Path)
    target.add_argument('--miner', type=Path)
    args = parser.parse_args()
    binary = (args.proxy or args.miner).resolve()
    root = Path(tempfile.mkdtemp(prefix='discord-notification-test-'))
    print(f'Test artifacts: {root}', flush=True)
    modes = ['nicehash', 'simple', 'extra_nonce'] if args.proxy else ['miner']
    for mode in modes:
        run_case(binary, root, mode, 'SAL')
    run_case(binary, root, modes[0], None)
    run_case(binary, root, modes[0], 'SAL', batch=True)


if __name__ == '__main__':
    main()
