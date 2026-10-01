#!/usr/bin/env python3
"""Local-only network validation regressions; never contacts mining pools."""
import argparse
import base64
import concurrent.futures
import http.client
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def request(port, path, value=None, auth='Bearer security-test'):
    conn = http.client.HTTPConnection('127.0.0.1', port, timeout=5)
    try:
        body = None if value is None else json.dumps(value)
        headers = {'Authorization': auth, 'Content-Type': 'application/json'} if auth else {}
        conn.request('GET' if value is None else 'POST', path, body, headers)
        res = conn.getresponse()
        result = res.status, res.read()
        return result
    finally:
        conn.close()


def raw(port, pieces):
    data = b''
    with socket.create_connection(('127.0.0.1', port), timeout=5) as sock:
        try:
            for piece in pieces:
                sock.sendall(piece)
                time.sleep(0.015)
            while True:
                part = sock.recv(65536)
                if not part:
                    break
                data += part
        except (BrokenPipeError, ConnectionResetError):
            pass
    return data


def run(binary, kind, empty_token=False):
    port, miner_port = free_port(), free_port()
    admin = 'Basic ' + base64.b64encode(b'security-test:security-test').decode()
    with tempfile.TemporaryDirectory(prefix='xmrig-security-') as directory:
        root = Path(directory)
        if kind == 'server':
            (root / 'updates').mkdir()
            (root / 'configs').mkdir()
            config = {'bind-ip': '127.0.0.1', 'port': port, 'user': 'security-test',
                      'pass': 'security-test', 'access-token': '' if empty_token else 'security-test',
                      'use-tls': False, 'background': False, 'colors': False,
                      'client-config-folder': str(root / 'configs'), 'client-update-folder': str(root / 'updates')}
        else:
            config = {'autosave': False, 'watch': False, 'background': False, 'colors': False,
                      'donate-level': 0, 'discord': {'enabled': False}, 'cc-client': {'enabled': False},
                      'http': {'enabled': True, 'host': '127.0.0.1', 'port': port,
                               'access-token': 'security-test', 'restricted': False},
                      'pools': [{'url': '127.0.0.1:9', 'user': 'security-test', 'pass': 'x', 'algo': 'rx/zecnero'}]}
            if kind == 'proxy':
                config.update({'mode': 'nicehash', 'donate-over-proxy': False,
                               'bind': [{'host': '127.0.0.1', 'port': miner_port}]})
            else:
                config.update({'cpu': {'enabled': True, 'huge-pages': False, 'rx': [-1]},
                               'randomx': {'rdmsr': False, 'wrmsr': False}, 'dmi': False,
                               'opencl': False, 'cuda': False})
        path = root / 'config.json'
        path.write_text(json.dumps(config))
        args = [str(binary), '-c', str(path)] + (['--daemonized'] if kind == 'miner' else [])
        proc = subprocess.Popen(args, cwd=root, stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        # Drain logs so logging cannot block the local server during concurrent requests.
        def drain_logs():
            for _ in iter(proc.stdout.readline, b''):
                pass
        reader = threading.Thread(target=drain_logs, daemon=True)
        reader.start()
        try:
            for _ in range(200):
                assert proc.poll() is None, 'process stopped before accepting requests'
                try:
                    with socket.create_connection(('127.0.0.1', port), timeout=.1):
                        break
                except OSError:
                    time.sleep(.05)
            else:
                raise AssertionError('HTTP listener did not start')
            if kind == 'server':
                status_path = '/client/setClientStatus?clientId=security-test'
                valid = {'client_status': {'client_id': 'security-test', 'hashrate_short': 42.5,
                                          'current_status': 'RUNNING', 'current_algo_name': 'rx/zecnero'}}
                if empty_token:
                    assert request(port, status_path, valid, auth=None)[0] == 403, 'empty client token must deny access'
                    return
                assert request(port, '/admin/getClientStatusList', auth=None)[0] == 401
                assert request(port, status_path, valid)[0] == 200, 'valid status must remain supported'
                invalid = [[], {}, {'client_status': []}, {'client_status': {'current_status': 0}},
                           {'client_status': {'hashrate_short': 'invalid'}}, {'client_status': {'cpu_threads': []}},
                           {'client_status': {'nonce_prefix': -1}}, {'client_status': {'gpu_info_list': {}}},
                           {'client_status': {'gpu_info_list': [None]}},
                           {'client_status': {'gpu_info_list': [{'gpu_info': {'name': 'test', 'memory': []}}]}}]
                for value in invalid:
                    assert request(port, status_path, value)[0] == 400, 'malformed status must be rejected'
                    assert proc.poll() is None, 'server must survive malformed status'
                command_path = '/admin/setClientCommand?clientId=security-test'
                for value in [[], {}, {'control_command': []}, {'control_command': {'command': 2}},
                              {'control_command': {'command': 'START', 'payload': []}}]:
                    assert request(port, command_path, value, admin)[0] == 400, 'malformed command must be rejected'
                def traffic(i):
                    if i % 2:
                        return request(port, status_path, valid)[0]
                    return request(port, '/admin/getClientStatusList', auth=admin)[0]
                with concurrent.futures.ThreadPoolExecutor(max_workers=12) as executor:
                    assert all(code == 200 for code in executor.map(traffic, range(120)))
            else:
                endpoint = '/1/summary' if kind == 'proxy' else '/2/summary'
                expected = request(port, endpoint)[0]
                assert expected == 200, 'authorized summary must remain available'
                code, encoded = request(port, '/1/config')
                exported = json.loads(encoded)
                assert code == 200 and exported['tls-allow-untrusted'] is True, 'untrusted default is serialized'
                exported['tls-allow-untrusted'] = False
                assert request(port, '/1/config', exported)[0] == 204, 'strict mode reload'
                assert json.loads(request(port, '/1/config')[1])['tls-allow-untrusted'] is False
                exported['tls-allow-untrusted'] = True
                assert request(port, '/1/config', exported)[0] == 204, 'permissive mode reload'
                assert json.loads(request(port, '/1/config')[1])['tls-allow-untrusted'] is True
                assert request(port, endpoint, auth=None)[0] == 401
                split = raw(port, [b'GET ' + endpoint[:3].encode(), endpoint[3:].encode(),
                                   b' HTTP/1.1\r\nHost: localhost\r\nAuthorization: Bearer security-test\r\n\r\n'])
                assert split.startswith(b'HTTP/1.1 200'), 'fragmented URL must route correctly'
                for pieces in [
                    [b'GET /' + b'x' * 8200 + b' HTTP/1.1\r\n\r\n'],
                    [b'GET / HTTP/1.1\r\nX-Test: ' + b'x' * 66000 + b'\r\n\r\n'],
                    [b'POST / HTTP/1.1\r\nContent-Length: 9000000\r\n\r\n'],
                    [b'POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n',
                     b'800001\r\n' + b'x' * (8 * 1024 * 1024 + 1) + b'\r\n0\r\n\r\n']]:
                    result = raw(port, pieces)
                    assert not result.startswith(b'HTTP/1.1 200'), 'oversized input must not be accepted'
                    assert request(port, endpoint)[0] == 200, 'HTTP listener remains healthy'
                if kind == 'proxy':
                    invalid = [{'padding': 'x' * 40}, {'id': 1, 'method': None, 'params': {}},
                               {'id': 1, 'method': 'login', 'params': []},
                               {'id': 1, 'method': 'login', 'params': {'login': 'test', 'algo': [None]}}]
                    for value in invalid:
                        raw(miner_port, [json.dumps(value).encode() + b'\n'])
                        assert proc.poll() is None, 'proxy remains healthy after malformed login'
                        assert request(port, endpoint)[0] == 200
            print(f'PASS {kind}: network validation, request bounds and service health', flush=True)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
                raise AssertionError('service did not stop promptly')
            reader.join(timeout=2)
            proc.stdout.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', required=True, type=lambda x: Path(x).resolve())
    parser.add_argument('--kind', required=True, choices=['proxy', 'miner', 'server'])
    args = parser.parse_args()
    run(args.binary, args.kind)
    if args.kind == 'server':
        run(args.binary, args.kind, empty_token=True)
        print('PASS server: missing client token denies access', flush=True)
