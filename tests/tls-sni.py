#!/usr/bin/env python3
"""Check SNI on actual Stratum and HTTPS daemon connections using loopback TLS."""
import argparse
import json
import socket
import socketserver
import ssl
import subprocess
import tempfile
import threading
from pathlib import Path


def run(binary):
    with tempfile.TemporaryDirectory(prefix='xmrig-sni-') as directory:
        root = Path(directory)
        cert, key = root / 'cert.pem', root / 'key.pem'
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                        '-days', '1', '-subj', '/CN=localhost', '-keyout', str(key),
                        '-out', str(cert)], check=True, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
        for daemon in (False, True):
            for host in ('127.0.0.1', '::1', 'localhost'):
                connected = threading.Event()
                names, requests, errors = [], [], []
                context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
                context.load_cert_chain(cert, key)
                context.set_servername_callback(lambda sock, name, ctx: names.append(name))

                class Handler(socketserver.BaseRequestHandler):
                    def handle(self):
                        try:
                            self.request.settimeout(5)
                            with context.wrap_socket(self.request, server_side=True) as sock:
                                requests.append(sock.recv(32768))
                                connected.set()
                        except (ssl.SSLError, OSError) as error:
                            errors.append(str(error))
                            connected.set()

                class Server(socketserver.ThreadingTCPServer):
                    address_family = socket.AF_INET6 if host == '::1' else socket.AF_INET
                    daemon_threads = True

                with Server(('::1' if host == '::1' else '127.0.0.1', 0), Handler) as server:
                    thread = threading.Thread(target=server.serve_forever, daemon=True)
                    thread.start()
                    url_host = '[' + host + ']' if host == '::1' else host
                    config = {'autosave': False, 'watch': False, 'background': False,
                              'donate-level': 0, 'tls-allow-untrusted': True,
                              'cc-client': {'enabled': False}, 'discord': {'enabled': False},
                              'http': {'enabled': False}, 'cpu': {'enabled': True, 'rx': [-1]},
                              'randomx': {'rdmsr': False, 'wrmsr': False}, 'dmi': False,
                              'opencl': False, 'cuda': False,
                              'pools': [{'url': f'{url_host}:{server.server_address[1]}',
                                         'user': 'sni-test', 'pass': 'x', 'algo': 'rx/zecnero',
                                         'tls': True, 'sni': True, 'daemon': daemon}]}
                    path = root / 'config.json'
                    path.write_text(json.dumps(config))
                    proc = subprocess.Popen([str(binary), '-c', str(path), '--daemonized'],
                                            cwd=root, stdin=subprocess.DEVNULL,
                                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    try:
                        assert connected.wait(15), f'{host}, daemon={daemon}: no connection'
                        expected = 'localhost' if host == 'localhost' else None
                        assert names and all(name == expected for name in names), (host, names)
                        assert requests and requests[0], (host, errors)
                        if daemon:
                            assert requests[0].startswith(b'POST '), requests[0]
                        else:
                            assert b'"login"' in requests[0], requests[0]
                    finally:
                        proc.terminate()
                        try:
                            proc.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            proc.kill()
                            proc.wait()
                        server.shutdown()
                        thread.join()
                print(f'PASS {"HTTPS daemon" if daemon else "Stratum"} {host}: SNI={expected!r}', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=lambda value: Path(value).resolve())
    run(parser.parse_args().binary)
