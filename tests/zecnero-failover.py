#!/usr/bin/env python3
"""Verify syncing RPC -> Stratum fallback -> recovered RPC -> fallback again."""
import base64
import copy
import http.server
import json
import pathlib
import signal
import socketserver
import struct
import subprocess
import sys
import tempfile
import threading
import time


def main():
    root = pathlib.Path(tempfile.mkdtemp(prefix='zecnero-failover-'))
    template = json.loads((pathlib.Path(__file__).parent / 'zecnero-template.json').read_text())
    template['capabilities'] = ['mineraddress']
    # Both connection modes use the same seed; the templates still have different jobs.
    blob = (struct.pack('<I', template['version'])
            + bytes.fromhex(template['previousblockhash'])[::-1]
            + bytes.fromhex(template['defaultroots']['merkleroot'])[::-1]
            + bytes.fromhex(template['blockcommitmentshash'])[::-1]
            + struct.pack('<II', template['curtime'], int(template['bits'], 16))
            + bytes(32)).hex()
    cookie = root / '.cookie'
    cookie.write_text('__cookie__:failover-test\n')
    state = {'ready': False, 'polls': 0, 'node_submits': 0,
             'pool_logins': 0, 'pool_submits': 0, 'errors': []}
    lock = threading.Lock()

    class Node(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            try:
                assert self.headers.get('Authorization') == 'Basic ' + base64.b64encode(b'__cookie__:failover-test').decode()
                req = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                reply = {'jsonrpc': '2.0', 'id': req['id']}
                with lock:
                    if req['method'] == 'getblocktemplate':
                        assert req['params'] == [{'mineraddress': 'test-payout-wallet'}]
                        state['polls'] += 1
                        if state['ready']:
                            reply['result'] = copy.deepcopy(template)
                        else:
                            reply['error'] = {'code': -10, 'message': 'node is syncing'}
                    else:
                        assert req['method'] == 'submitblock'
                        state['node_submits'] += 1
                        reply['result'] = None
                body = json.dumps(reply).encode()
                self.send_response(200)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass
            except Exception as exc:
                state['errors'].append(str(exc))

    class Pool(socketserver.StreamRequestHandler):
        def handle(self):
            try:
                for line in self.rfile:
                    req = json.loads(line)
                    result = {'status': 'OK'}
                    with lock:
                        if req['method'] == 'login':
                            assert req['params']['login'] == 'pool-wallet.worker'
                            state['pool_logins'] += 1
                            assert state['polls'] >= 3, 'fallback bypassed the retry count'
                            result.update(id='pool-session-' + str(state['pool_logins']), job={
                                'job_id': 'pool-job-' + str(state['pool_logins']),
                                'algo': 'rx/zecnero', 'blob': blob,
                                'target': 'ffffffffffffffff',
                                'seed_hash': template['seedhash'], 'height': template['height']})
                        elif req['method'] == 'submit':
                            assert len(bytes.fromhex(req['params']['nonce'])) == 4
                            assert len(bytes.fromhex(req['params']['result'])) == 32
                            state['pool_submits'] += 1
                        else:
                            assert req['method'] == 'keepalived'
                    self.wfile.write((json.dumps({'id': req['id'], 'jsonrpc': '2.0',
                                                 'result': result, 'error': None}) + '\n').encode())
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                pass
            except Exception as exc:
                state['errors'].append(str(exc))

    class PoolServer(socketserver.ThreadingTCPServer):
        daemon_threads = True

    node = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Node)
    pool = PoolServer(('127.0.0.1', 0), Pool)
    for server in (node, pool):
        threading.Thread(target=server.serve_forever, daemon=True).start()
    log = root / 'events.log'
    config = {
        'autosave': False, 'background': False, 'colors': False, 'watch': False,
        'dmi': False, 'donate-level': 0, 'retries': 3, 'retry-pause': 1,
        'log-file': str(log), 'print-time': 5,
        'randomx': {'mode': 'light', 'init': 1, 'init-avx2': -1,
                    'rdmsr': False, 'wrmsr': False, 'numa': False},
        'cpu': {'enabled': True, 'huge-pages': False, 'rx/zecnero': [-1]},
        'opencl': False, 'cuda': False, 'cc-client': {'enabled': False},
        'pools': [
            {'url': f'127.0.0.1:{node.server_port}', 'algo': 'rx/zecnero',
             'daemon': True, 'daemon-cookie-file': str(cookie),
             'daemon-poll-interval': 1000, 'user': 'test-payout-wallet'},
            {'url': f'127.0.0.1:{pool.server_address[1]}', 'algo': 'rx/zecnero',
             'daemon': False, 'user': 'pool-wallet.worker', 'pass': 'x'}]
    }
    (root / 'config.json').write_text(json.dumps(config))
    with (root / 'stdout.log').open('wb') as output:
        proc = subprocess.Popen([str(pathlib.Path(sys.argv[1]).resolve()), '--daemonized',
                                 '-c', str(root / 'config.json')], cwd=root,
                                stdout=output, stderr=subprocess.STDOUT)

    def wait_for(condition):
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            assert not state['errors'], state['errors']
            assert proc.poll() is None, 'miner exited early'
            if condition():
                return
            time.sleep(.05)
        raise AssertionError(f'timed out: {state}; logs: {root}')

    try:
        wait_for(lambda: state['polls'] >= 2)
        assert state['pool_logins'] == 0, 'switched before configured retries'
        wait_for(lambda: state['pool_submits'] >= 1)
        assert state['node_submits'] == 0, 'mined to a syncing node'
        before = state['polls']
        wait_for(lambda: state['polls'] >= before + 2)
        state['ready'] = True
        wait_for(lambda: state['node_submits'] >= 1)
        time.sleep(.3)  # Finish any share already in flight on the closed backup.
        submitted = state['pool_submits']
        before = state['node_submits']
        wait_for(lambda: state['node_submits'] > before)
        assert state['pool_submits'] == submitted, 'backup still mining after primary recovered'
        state['ready'] = False
        wait_for(lambda: state['pool_logins'] >= 2 and state['pool_submits'] > submitted)
        time.sleep(.3)
        submitted = state['node_submits']
        before = state['polls']
        wait_for(lambda: state['polls'] >= before + 2)
        assert state['node_submits'] == submitted, 'node submissions continued while syncing'
        state['ready'] = True
        wait_for(lambda: state['node_submits'] > submitted)
        print('PASS: syncing primary triggers Stratum fallback after retries; primary polling, recovery and repeated fallback work')
    finally:
        if proc.poll() is None:
            proc.send_signal(signal.SIGINT)
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        for server in (node, pool):
            server.shutdown()
            server.server_close()
        print(f'Failover test artifacts: {root}')


if __name__ == '__main__':
    main()
