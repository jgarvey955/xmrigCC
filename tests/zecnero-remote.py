#!/usr/bin/env python3
"""End-to-end HTTPS cookie retrieval, multiple logins and both payout modes."""
import argparse
import base64
import hashlib
import json
import pathlib
import signal
import socket
import ssl
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor


def port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def stop(proc):
    if proc and proc.poll() is None:
        proc.send_signal(signal.SIGINT)
        try:
            proc.wait(20)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--node', required=True, type=pathlib.Path)
    parser.add_argument('--miner', required=True, type=pathlib.Path)
    args = parser.parse_args()
    root = pathlib.Path(tempfile.mkdtemp(prefix='zecnero-remote-test-'))
    rpc_port, tls_port, p2p_port = port(), port(), port()
    default_address = 'nmH2sBPxwa1KyUGqPf37WMsk9ZQrWM7uZ67'
    miner_address = 'nm9S6MZjGK8XBzbXoTsRfSUVAkMA4sFzM5r'
    login1 = 'miner1:regtest-password-one'
    login2 = 'miner2:regtest-password-two'
    cert, key = root / 'server.pem', root / 'server.key'
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                    '-subj', '/CN=localhost', '-addext', 'subjectAltName=IP:127.0.0.1',
                    '-keyout', str(key), '-out', str(cert)], check=True, capture_output=True)
    fingerprint = hashlib.sha256(ssl.PEM_cert_to_DER_cert(cert.read_text())).hexdigest()
    context = ssl.create_default_context(cafile=str(cert))
    node_config = root / 'node.toml'

    def write_node(with_default=True):
        node_config.write_text(f'''[network]
network = "Regtest"
listen_addr = "127.0.0.1:{p2p_port}"
[network.testnet_parameters]
experimental_randomx_v2 = true
[state]
cache_dir = "{root}/state"
[rpc]
listen_addr = "127.0.0.1:{rpc_port}"
cookie_dir = "{root}/rpc"
zecnero-cookie-cred = ["{login1}", "{login2.replace(':', ',', 1)}"]
[rpc.cookie_endpoint]
listen_addr = "127.0.0.1:{tls_port}"
cert_file = "{cert}"
key_file = "{key}"
[mining]
''' + (f'miner_address = "{default_address}"\n' if with_default else '') + '[tracing]\nuse_color = false\n')

    def rpc(method, params):
        cookie = (root / 'rpc/.cookie').read_bytes().strip()
        request = urllib.request.Request(f'http://127.0.0.1:{rpc_port}/',
            data=json.dumps({'jsonrpc': '2.0', 'id': 1, 'method': method, 'params': params}).encode(),
            headers={'Content-Type': 'application/json', 'Authorization': 'Basic ' + base64.b64encode(cookie).decode()})
        data = json.load(urllib.request.urlopen(request, timeout=10))
        if data.get('error'):
            raise RuntimeError(data['error']['message'])
        return data['result']

    def retrieve(login):
        headers = {'Authorization': 'Basic ' + base64.b64encode(login.encode()).decode()} if login else {}
        request = urllib.request.Request(f'https://127.0.0.1:{tls_port}', headers=headers)
        with urllib.request.urlopen(request, context=context, timeout=5) as response:
            assert response.headers['Cache-Control'] == 'no-store'
            return response.read()

    def start_node():
        with (root / 'node.log').open('ab') as log:
            proc = subprocess.Popen([str(args.node.resolve()), '-c', str(node_config)], cwd=root, stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            try:
                rpc('getblockchaininfo', [])
                retrieve(login1)
                return proc
            except (OSError, RuntimeError):
                if proc.poll() is not None:
                    raise RuntimeError('node exited during startup')
                time.sleep(0.2)
        stop(proc)
        raise RuntimeError('node startup timed out')

    config = {
        'autosave': False, 'background': False, 'colors': False, 'watch': False,
        'donate-level': 0, 'retry-pause': 1, 'dmi': False, 'log-file': str(root / 'events.log'),
        'randomx': {'mode': 'fast', 'init': 2, 'rdmsr': False, 'wrmsr': False, 'numa': False},
        'cpu': {'enabled': True, 'huge-pages': False, 'rx/zecnero': [-1], 'rx/zecnero2': [-1]},
        'opencl': False, 'cuda': False, 'cc-client': {'enabled': False},
        'pools': [{'url': f'127.0.0.1:{rpc_port}', 'algo': 'rx2/zecnero', 'daemon': True,
                   'user': miner_address, 'daemon-cookie-file': str(root / 'miner.cookie'),
                   'daemon-cookie-source': f'https://127.0.0.1:{tls_port}',
                   'daemon-cookie-auth': login1, 'daemon-cookie-fingerprint': fingerprint,
                   'daemon-poll-interval': 1000}]
    }

    def start_miner():
        (root / 'miner.json').write_text(json.dumps(config))
        with (root / 'miner.log').open('ab') as log:
            return subprocess.Popen([str(args.miner.resolve()), '--daemonized', '-c', str(root / 'miner.json')],
                                    cwd=root, stdout=log, stderr=subprocess.STDOUT)

    def wait_height(proc, height):
        deadline = time.monotonic() + 100
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                raise RuntimeError('miner exited early')
            if rpc('getblockchaininfo', [])['blocks'] >= height:
                return
            time.sleep(0.2)
        raise RuntimeError('mining timed out')

    node = miner = None
    try:
        write_node()
        node = start_node()
        for invalid in ['', 'miner1:wrong', 'unknown:regtest-password-one']:
            try:
                retrieve(invalid)
                raise AssertionError('unauthorized retrieval succeeded')
            except urllib.error.HTTPError as error:
                assert error.code == 401
        assert retrieve(login1) == retrieve(login2) == (root / 'rpc/.cookie').read_bytes()
        default = rpc('getblocktemplate', [{}])
        assert default['height'] == 1 and default['powversion'] == 1 and default['algo'] == 'rx/zecnero'
        with ThreadPoolExecutor(max_workers=2) as executor:
            requested = executor.submit(rpc, 'getblocktemplate', [{'mineraddress': miner_address}])
            fallback = executor.submit(rpc, 'getblocktemplate', [{}])
            requested, fallback = requested.result(), fallback.result()
        assert requested['coinbasetxn']['data'] != fallback['coinbasetxn']['data']
        assert fallback['coinbasetxn']['data'] == default['coinbasetxn']['data']
        try:
            rpc('getblocktemplate', [{'mineraddress': 'invalid-wallet'}])
            raise AssertionError('invalid payout silently used the default')
        except RuntimeError:
            pass

        # Neither a bad login nor an incorrect TLS certificate pin may create a cookie.
        for bad_login, bad_pin in [('miner1:wrong', fingerprint), (login1, '00' * 32)]:
            config['pools'][0]['daemon-cookie-auth'] = bad_login
            config['pools'][0]['daemon-cookie-fingerprint'] = bad_pin
            miner = start_miner()
            time.sleep(2)
            stop(miner)
            assert not (root / 'miner.cookie').exists()
            assert not (root / 'miner.cookie.fingerprint').exists()
            assert rpc('getblockchaininfo', [])['blocks'] == 0
        config['pools'][0]['daemon-cookie-auth'] = login1
        config['pools'][0].pop('daemon-cookie-fingerprint', None)
        miner = start_miner()
        wait_height(miner, 3)
        stop(miner)
        assert (root / 'miner.cookie').read_bytes() == retrieve(login1)
        assert (root / 'miner.cookie').stat().st_mode & 0o777 == 0o600
        pin_file = root / 'miner.cookie.fingerprint'
        saved_pin = pin_file.read_text()
        assert saved_pin.splitlines() == [f'https://127.0.0.1:{tls_port}', fingerprint]
        assert pin_file.stat().st_mode & 0o777 == 0o600
        # On subsequent starts, the saved pin must be enforced even with no pin in config.
        old_cookie = (root / 'miner.cookie').read_bytes()
        before = rpc('getblockchaininfo', [])['blocks']
        pin_file.write_text(f'https://127.0.0.1:{tls_port}\n' + '00' * 32 + '\n')
        miner = start_miner()
        time.sleep(2)
        stop(miner)
        assert (root / 'miner.cookie').read_bytes() == old_cookie
        assert rpc('getblockchaininfo', [])['blocks'] == before
        pin_file.write_text(saved_pin)
        print('PASS: automatic first-use certificate pin, owner-only persistence, saved-pin mismatch rejection')
        raw = bytes.fromhex(rpc('getblock', ['1', 0]))
        assert raw[140:142] == b'\x00\x01'
        assert raw[142:] == bytes.fromhex(requested['coinbasetxn']['data'])
        assert rpc('getaddressbalance', [{'addresses': [miner_address]}])['balance'] > 0
        assert rpc('getaddressbalance', [{'addresses': [default_address]}])['balance'] == 0
        logs = (root / 'events.log').read_text()
        assert 'algo rx/zecnero height 1' in logs
        assert 'algo rx/zecnero2 height 2' in logs
        print('PASS: HTTPS auth/pin failures, multiple logins, automatic cookie creation, wallet override, automatic v1-to-v2 fast mining at height 2')

        # Delete the local cookie while mining; the client must retrieve it again.
        config['pools'][0]['user'] = ''
        config['pools'][0]['daemon-cookie-auth'] = login2
        before = rpc('getblockchaininfo', [])['blocks']
        miner = start_miner()
        wait_height(miner, before + 1)
        (root / 'miner.cookie').unlink()
        # Easy Regtest blocks can advance several heights between polls. Wait for
        # the actual cookie refresh, not a height that may already be reached.
        expected_cookie = retrieve(login2)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if miner.poll() is not None:
                raise RuntimeError('miner exited before recreating its cookie')
            try:
                if (root / 'miner.cookie').read_bytes() == expected_cookie:
                    break
            except FileNotFoundError:
                pass
            time.sleep(0.1)
        else:
            raise RuntimeError('deleted cookie was not retrieved again')
        wait_height(miner, rpc('getblockchaininfo', [])['blocks'] + 1)
        assert (root / 'miner.cookie').read_bytes() == expected_cookie
        stop(miner)
        assert rpc('getaddressbalance', [{'addresses': [default_address]}])['balance'] > 0
        print('PASS: second miner login, daemon payout fallback, deleted cookie retrieval')

        # Keep the miner alive across a real daemon restart and cookie rotation.
        old = (root / 'rpc/.cookie').read_bytes()
        before = rpc('getblockchaininfo', [])['blocks']
        miner = start_miner()
        stop(node)
        node = start_node()
        assert old != (root / 'rpc/.cookie').read_bytes()
        wait_height(miner, before + 1)
        assert (root / 'miner.cookie').read_bytes() == retrieve(login2)
        stop(miner)
        print('PASS: automatic retrieval after daemon restart and cookie rotation')

        stop(node)
        write_node(with_default=False)
        node = start_node()
        try:
            rpc('getblocktemplate', [{}])
            raise AssertionError('missing payout was accepted')
        except RuntimeError:
            pass
        assert rpc('getblocktemplate', [{'mineraddress': miner_address}])['powversion'] == 2
        config['pools'][0]['user'] = miner_address
        miner = start_miner()
        wait_height(miner, rpc('getblockchaininfo', [])['blocks'] + 1)
        stop(miner)
        print('PASS: miner wallet works without a daemon wallet')
        logs = (root / 'events.log').read_text() + (root / 'node.log').read_text()
        for secret in [login1, login2, login1.split(':')[1], login2.split(':')[1], old.decode()]:
            assert secret not in logs, 'credentials leaked in logs'
    finally:
        stop(miner)
        stop(node)
        print(f'Test artifacts: {root}')


if __name__ == '__main__':
    main()
