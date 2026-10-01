#!/usr/bin/env python3
"""Exercise certificate trust, host validation and explicit pins on loopback only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import socketserver
import ssl
import subprocess
import tempfile
import threading
import time


def certificate(root, name):
    cert, key = root / (name + '.crt'), root / (name + '.key')
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                    '-subj', '/CN=' + name, '-addext', 'subjectAltName=DNS:' + name,
                    '-keyout', str(key), '-out', str(cert)], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return cert, key


def run(binary, proxy):
    with tempfile.TemporaryDirectory(prefix='xmrig-tls-verify-') as directory:
        root = Path(directory)
        valid = certificate(root, 'localhost')
        wrong = certificate(root, 'wrong.invalid')
        pin = hashlib.sha256(ssl.PEM_cert_to_DER_cert(valid[0].read_text())).hexdigest()
        cases = [('pinned', valid, pin, None, True), ('bad-pin', valid, '0' * 64, None, False),
                 ('untrusted', valid, None, None, False), ('trusted', valid, None, valid[0], True),
                 ('wrong-host', wrong, None, wrong[0], False), ('long-pin', valid, pin + '00', None, False),
                 ('allow-untrusted', valid, None, None, True),
                 ('default-untrusted', valid, None, None, True),
                 ('allow-untrusted-bad-pin', valid, '0' * 64, None, False)]
        for name, cert_key, fingerprint, ca, success in cases:
            attempted, login = threading.Event(), threading.Event()
            errors, output = [], []
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(*cert_key)

            class Handler(socketserver.BaseRequestHandler):
                def handle(self):
                    self.request.settimeout(3)
                    try:
                        with context.wrap_socket(self.request, server_side=True) as sock:
                            attempted.set()
                            data = sock.recv(32768)
                            if b'"login"' in data:
                                login.set()
                    except (ssl.SSLError, OSError) as error:
                        errors.append(str(error))
                        attempted.set()

            class Server(socketserver.ThreadingTCPServer):
                daemon_threads = True
                allow_reuse_address = True

            with Server(('127.0.0.1', 0), Handler) as server:
                thread = threading.Thread(target=server.serve_forever, daemon=True)
                thread.start()
                pool = {'url': f'localhost:{server.server_address[1]}', 'user': 'tls-security-test',
                        'pass': 'x', 'algo': 'rx/zecnero', 'tls': True}
                if fingerprint is not None:
                    pool['tls-fingerprint'] = fingerprint
                config = {'autosave': False, 'watch': False, 'background': False, 'colors': False,
                          'donate-level': 0, 'discord': {'enabled': False}, 'cc-client': {'enabled': False},
                          'http': {'enabled': False}, 'pools': [pool],
                          'tls-allow-untrusted': name.startswith('allow-untrusted')}
                if name == 'default-untrusted':
                    config.pop('tls-allow-untrusted')
                if proxy:
                    with socket.socket() as sock:
                        sock.bind(('127.0.0.1', 0))
                        port = sock.getsockname()[1]
                    config.update({'mode': 'nicehash', 'donate-over-proxy': False,
                                   'bind': [{'host': '127.0.0.1', 'port': port}]})
                else:
                    config.update({'cpu': {'enabled': True, 'huge-pages': False, 'rx': [-1]},
                                   'randomx': {'rdmsr': False, 'wrmsr': False}, 'dmi': False,
                                   'opencl': False, 'cuda': False})
                path = root / (name + '.json')
                path.write_text(json.dumps(config))
                env = dict(os.environ)
                env.pop('SSL_CERT_FILE', None)
                env.pop('SSL_CERT_DIR', None)
                if ca:
                    env['SSL_CERT_FILE'] = str(ca)
                args = [str(binary), '-c', str(path)] + ([] if proxy else ['--daemonized'])
                proc = subprocess.Popen(args, cwd=root, env=env, stdin=subprocess.DEVNULL,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                def read_logs():
                    for line in iter(proc.stdout.readline, b''):
                        output.append(line.decode(errors="replace"))
                        del output[:-20]
                drain = threading.Thread(target=read_logs, daemon=True)
                drain.start()
                try:
                    assert attempted.wait(10), f'{name}: no TLS connection attempt'
                    observed = login.wait(3 if success else .6)
                    assert observed == success, f'{name}: incorrect certificate acceptance; TLS errors={errors}; log={output[-12:]}'
                    assert proc.poll() is None, f'{name}: process stopped'
                finally:
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait()
                    drain.join(timeout=2)
                    proc.stdout.close()
                    server.shutdown()
                    thread.join()
                print(f'PASS TLS {name}', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', required=True, type=lambda x: Path(x).resolve())
    parser.add_argument('--proxy', action='store_true')
    args = parser.parse_args()
    run(args.binary, args.proxy)
