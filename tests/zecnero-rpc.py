#!/usr/bin/env python3
"""Exercise the real miner against controlled Zecnero RPC responses (no network peers)."""
import base64
import copy
import http.server
import json
import pathlib
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time


def main():
    root = pathlib.Path(tempfile.mkdtemp(prefix='zecnero-rpc-test-'))
    fixture = json.loads((pathlib.Path(__file__).parent / 'zecnero-template.json').read_text())
    cookie = root / '.cookie'
    cookie.write_text('__cookie__:mock-a\n')
    state = {'templates': 0, 'submits': 0, 'accepted': 0, 'rotated': False, 'errors': []}
    lock = threading.Lock()
    delayed_poll = threading.Event()
    delayed_done = threading.Event()

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def reply(self, data, status=200):
            body = json.dumps(data).encode()
            try:
                self.send_response(status)
                self.send_header('Content-Type', 'application/json')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def do_POST(self):
            try:
                req = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                assert self.path == '/', 'wrong RPC path'
                assert req['jsonrpc'] == '2.0', 'wrong RPC version'
                auth = self.headers.get('Authorization', '')
                if auth == 'Basic ' + base64.b64encode(b'__cookie__:mock-b').decode():
                    state['rotated'] = True
                assert auth in ['Basic ' + base64.b64encode(c).decode() for c in [b'__cookie__:mock-a', b'__cookie__:mock-b']], 'invalid HTTP Basic auth'
                response = {'jsonrpc': '2.0', 'id': req['id']}
                if req['method'] == 'getblocktemplate':
                    assert req['params'] == [{}], 'wrong template params'
                    with lock:
                        state['templates'] += 1
                        count = state['templates']
                    if count == 1:
                        self.reply({'error': 'test unauthorized'}, 401)
                        return
                    template = copy.deepcopy(fixture)
                    if count == 2 or state['accepted'] >= 2:
                        template['powversion'] = 3
                    if count == 4:
                        delayed_poll.set()
                        time.sleep(1.5)
                    response['result'] = template
                    self.reply(response)
                    if count == 4:
                        delayed_done.set()
                    return
                assert req['method'] == 'submitblock', 'unexpected RPC method'
                assert len(req['params']) == 1, 'wrong submit params'
                block = bytes.fromhex(req['params'][0])
                prefix = (struct.pack('<I', fixture['version']) + bytes.fromhex(fixture['previousblockhash'])[::-1]
                          + bytes.fromhex(fixture['defaultroots']['merkleroot'])[::-1]
                          + bytes.fromhex(fixture['blockcommitmentshash'])[::-1]
                          + struct.pack('<II', fixture['curtime'], int(fixture['bits'], 16)))
                assert block[:108] == prefix, 'corrupted header'
                assert block[140:] == b'\x00\x01' + bytes.fromhex(fixture['coinbasetxn']['data']), 'corrupted coinbase/serialization'
                assert any(block[112:140]), 'missing extranonce'
                with lock:
                    state['submits'] += 1
                    count = state['submits']
                if count == 1:
                    response['result'] = 'mock-rejection'
                elif count == 2:
                    assert delayed_poll.wait(3), 'no concurrent template poll'
                    assert not delayed_done.is_set(), 'submission waited for template poll'
                    response['error'] = {'code': -1, 'message': 'mock-rpc-rejection'}
                elif count == 3:
                    response['result'] = {'status': 'OK'}  # Monero shape must NOT count as accepted
                else:
                    time.sleep(0.15)
                    response['result'] = None
                    with lock:
                        state['accepted'] += 1
                    cookie.write_text('__cookie__:mock-b\n')
                self.reply(response)
            except Exception as exc:
                state['errors'].append(str(exc))
                self.reply({'error': 'test assertion failed'}, 500)

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    config = {
        'autosave': False, 'background': False, 'colors': False, 'watch': False,
        'donate-level': 0, 'retry-pause': 1, 'dmi': False, 'log-file': str(root / 'events.log'),
        'randomx': {'mode': 'light', 'init': 1, 'rdmsr': False, 'wrmsr': False, 'numa': False},
        'cpu': {'enabled': True, 'huge-pages': False, 'rx/zecnero': [-1]},
        'opencl': False, 'cuda': False, 'cc-client': {'enabled': False},
        'pools': [{'url': f'127.0.0.1:{server.server_port}', 'algo': 'rx/zecnero', 'daemon': True,
                   'daemon-cookie-file': str(cookie), 'daemon-poll-interval': 1000}]
    }
    (root / 'miner.json').write_text(json.dumps(config))
    with (root / 'stdout.log').open('wb') as log:
        proc = subprocess.Popen([str(pathlib.Path(sys.argv[1]).resolve()), '--daemonized', '-c', str(root / 'miner.json')], stdout=log, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            if state['errors']:
                raise RuntimeError(state['errors'])
            if state['accepted'] >= 2 and state['rotated'] and delayed_done.is_set():
                break
            if proc.poll() is not None:
                raise RuntimeError('miner exited early')
            time.sleep(0.1)
        else:
            raise RuntimeError(f'timed out: {state}')
        logs = (root / 'events.log').read_text()
        for expected in ['HTTP 401', 'unsupported Zecnero PoW version', 'mock-rejection', 'mock-rpc-rejection', 'invalid submitblock result']:
            if expected not in logs:
                raise RuntimeError(f'missing expected result: {expected}')
        if logs.count('Zecnero block accepted by node') != state['accepted']:
            raise RuntimeError('incorrect accepted block accounting')
        print('PASS: auth retry, cookie rotation, v2 rejection, independent polling/submission, stale response, serialization and null-only acceptance')
    finally:
        if proc.poll() is None:
            proc.send_signal(signal.SIGINT)
            try:
                proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        server.shutdown()
        server.server_close()
        print(f'RPC test artifacts: {root}')


if __name__ == '__main__':
    main()
