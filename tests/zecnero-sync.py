#!/usr/bin/env python3
"""The real miner must wait for sync, pause on catch-up, and resume automatically."""
import base64
import copy
import http.server
import hashlib
import json
import pathlib
import signal
import ssl
import subprocess
import sys
import tempfile
import threading
import time


def main():
    root = pathlib.Path(tempfile.mkdtemp(prefix='zecnero-sync-test-'))
    fixture = json.loads((pathlib.Path(__file__).parent / 'zecnero-template.json').read_text())
    fixture['capabilities'] = ['mineraddress']
    source = root / 'node.cookie'
    source.write_text('__cookie__:sync-test\n')
    cookie = root / 'miner.cookie'
    log = root / 'events.log'
    use_https = '--https' in sys.argv[2:]
    state = {'ready': False, 'polls': 0, 'submits': 0, 'errors': [],
             'credential': '__cookie__:sync-test', 'cookie_gets': 0}
    lock = threading.Lock()

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            response = {}
            try:
                req = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                if self.headers.get('Authorization') != 'Basic ' + base64.b64encode(state['credential'].encode()).decode():
                    self.send_response(401)
                    self.send_header('Content-Length', '0')
                    self.end_headers()
                    return
                response = {'jsonrpc': '2.0', 'id': req['id']}
                with lock:
                    if req['method'] == 'getblocktemplate':
                        assert req['params'] == [{'mineraddress': 'test-payout-wallet'}]
                        state['polls'] += 1
                        if state['ready']:
                            response['result'] = copy.deepcopy(fixture)
                        else:
                            response['error'] = {'code': -10, 'message': 'node is syncing'}
                    else:
                        assert req['method'] == 'submitblock'
                        state['submits'] += 1
                        response['result'] = None
            except Exception as exc:
                state['errors'].append(str(exc))
                response['error'] = {'code': -1, 'message': str(exc)}
            body = json.dumps(response).encode()
            try:
                self.send_response(200)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    cookie_server = None
    cookie_source = str(source)
    fingerprint = 'a' * 64
    if use_https:
        cert, key = root / 'cert.pem', root / 'key.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                        '-keyout', str(key), '-out', str(cert), '-days', '1',
                        '-subj', '/CN=localhost'], check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)

        class CookieHandler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                assert self.path == '/'
                assert self.headers.get('Authorization') == 'Basic ' + base64.b64encode(b'miner:test-only').decode()
                state['cookie_gets'] += 1
                body = state['credential'].encode()
                self.send_response(200)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        cookie_server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), CookieHandler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        cookie_server.socket = context.wrap_socket(cookie_server.socket, server_side=True)
        threading.Thread(target=cookie_server.serve_forever, daemon=True).start()
        cookie_source = f'https://127.0.0.1:{cookie_server.server_port}'
        fingerprint = hashlib.sha256(ssl.PEM_cert_to_DER_cert(cert.read_text())).hexdigest()
    config = {
        'autosave': False, 'background': False, 'colors': False, 'watch': False,
        'donate-level': 0, 'dmi': False, 'log-file': str(log),
        'randomx': {'mode': 'light', 'init': 1, 'rdmsr': False, 'wrmsr': False, 'numa': False},
        'cpu': {'enabled': True, 'huge-pages': False, 'rx/zecnero': [-1]},
        'opencl': False, 'cuda': False, 'cc-client': {'enabled': False}, 'pools': []
    }
    config_path = root / 'config.json'
    config_path.write_text(json.dumps(config))
    command = [str(pathlib.Path(sys.argv[1]).resolve()), '--daemonized', '-c', str(config_path),
               '--url', f'127.0.0.1:{server.server_port}', '--daemon', '--algo', 'rx/zecnero',
               '--user', 'test-payout-wallet', '--daemon-cookie-file', str(cookie),
               '--daemon-cookie-source', cookie_source, '--daemon-cookie-auth', 'miner:test-only',
               '--daemon-cookie-fingerprint', fingerprint, '--daemon-rpc-user', 'rpc-user']
    with (root / 'stdout.log').open('wb') as output:
        proc = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT)

    def logs():
        return log.read_text() if log.exists() else ''

    def wait_for(condition):
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            assert not state['errors'], state['errors']
            assert proc.poll() is None, 'miner exited early'
            if condition():
                return
            time.sleep(0.05)
        raise AssertionError(f'timed out: {state}')

    try:
        waiting = 'waiting for Zecnero daemon to finish syncing; mining paused'
        wait_for(lambda: state['polls'] >= 3)
        assert waiting in logs()
        assert 'init dataset' not in logs() and 'new job' not in logs()
        assert state['submits'] == 0
        if use_https:
            before = state['polls']
            state['credential'] = '__cookie__:rotated-during-sync'
            source.write_text(state['credential'] + '\n')
            wait_for(lambda: state['cookie_gets'] >= 2 and logs().count(waiting) >= 2)
            wait_for(lambda: state['polls'] >= before + 3)
            assert state['submits'] == 0 and 'init dataset' not in logs()
            assert cookie.read_text().strip() == state['credential']
        wait_messages = logs().count(waiting)
        state['ready'] = True
        wait_for(lambda: state['submits'] >= 1)
        state['ready'] = False
        wait_for(lambda: logs().count(waiting) == wait_messages + 1)
        # Let any already-sent request finish before measuring the paused interval.
        time.sleep(0.5)
        submitted, polled = state['submits'], state['polls']
        wait_for(lambda: state['polls'] >= polled + 3)
        assert state['submits'] == submitted, 'submitted work while node was syncing'
        assert 'paused' in logs()
        state['ready'] = True
        wait_for(lambda: state['submits'] > submitted)
        assert cookie.read_text().strip() == source.read_text().strip()
        print('PASS: CLI cookie options, no work before sync, pause on catch-up, automatic resume' +
              (' and HTTPS cookie rotation while waiting' if use_https else ''))
    finally:
        if proc.poll() is None:
            proc.send_signal(signal.SIGINT)
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        server.shutdown()
        server.server_close()
        if cookie_server:
            cookie_server.shutdown()
            cookie_server.server_close()
        print(f'Sync test artifacts: {root}')


if __name__ == '__main__':
    main()
