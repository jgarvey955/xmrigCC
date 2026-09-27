#!/usr/bin/env python3
"""Mine three blocks against an isolated, PoW-validating Zecnero Regtest node.

Usage: python3 tests/zecnero-regtest.py --node /path/to/zecnerod \
    --miner build/xmrigMiner
No existing node, cookie, config, or chain database is used. Logs are kept in /tmp.
"""
import argparse
import base64
import json
import pathlib
import re
import signal
import socket
import subprocess
import tempfile
import time
import urllib.request


def port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def stop(proc):
    if proc and proc.poll() is None:
        proc.send_signal(signal.SIGINT)
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--node', required=True, type=pathlib.Path)
    parser.add_argument('--miner', required=True, type=pathlib.Path)
    parser.add_argument('--mode', default='light', choices=['light', 'fast'])
    args = parser.parse_args()
    root = pathlib.Path(tempfile.mkdtemp(prefix='zecnero-xmrig-regtest-'))
    rpc_port = port()
    (root / 'node.toml').write_text(f'''[network]
network = "Regtest"
listen_addr = "127.0.0.1:{port()}"
[network.testnet_parameters]
experimental_randomx_v2 = true
[state]
ephemeral = true
[rpc]
listen_addr = "127.0.0.1:{rpc_port}"
enable_cookie_auth = true
cookie_dir = "{root}/rpc"
[mining]
# Public test-vector address; these Regtest coins have no value.
miner_address = "nmH2sBPxwa1KyUGqPf37WMsk9ZQrWM7uZ67"
[tracing]
use_color = false
''')
    config = {
        'autosave': False, 'background': False, 'colors': False, 'watch': False,
        'donate-level': 0, 'print-time': 5, 'dmi': False,
        'log-file': str(root / 'miner-events.log'),
        'randomx': {'mode': args.mode, 'init': 2, 'rdmsr': False, 'wrmsr': False, 'numa': False},
        'cpu': {'enabled': True, 'huge-pages': False, 'rx/zecnero': [-1], 'rx/zecnero2': [-1]},
        'opencl': False, 'cuda': False, 'cc-client': {'enabled': False},
        'pools': [{'algo': 'rx/zecnero', 'url': f'127.0.0.1:{rpc_port}',
                   'daemon': True, 'daemon-cookie-file': str(root / 'rpc/.cookie'),
                   'daemon-poll-interval': 1000, 'daemon-job-timeout': 15000}]
    }
    (root / 'miner.json').write_text(json.dumps(config))

    def rpc(method, params):
        cookie = (root / 'rpc/.cookie').read_bytes().strip()
        req = urllib.request.Request(f'http://127.0.0.1:{rpc_port}/',
            data=json.dumps({'jsonrpc': '2.0', 'id': 1, 'method': method, 'params': params}).encode(),
            headers={'Content-Type': 'application/json', 'Authorization': 'Basic ' + base64.b64encode(cookie).decode()})
        data = json.load(urllib.request.urlopen(req, timeout=5))
        if data.get('error'):
            raise RuntimeError(data['error'])
        return data['result']

    node = miner = None
    try:
        with (root / 'node.log').open('wb') as log:
            node = subprocess.Popen([str(args.node.resolve()), '-c', str(root / 'node.toml')], cwd=root, stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 60
        while True:
            try:
                template = rpc('getblocktemplate', [{}])
                break
            except (OSError, RuntimeError):
                if node.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError('Regtest node did not become ready')
                time.sleep(0.5)
        if template['height'] != 1 or template['bits'] != '207fffff' or template['powversion'] != 1 or template['algo'] != 'rx/zecnero':
            raise RuntimeError('Expected a fresh Zecnero Regtest chain with v1 at height 1')
        (root / 'template.json').write_text(json.dumps(template, indent=2))
        with (root / 'miner.log').open('wb') as log:
            # CC normally supplies this flag through xmrigDaemon; run the child
            # directly so the test can stop it without an auto-restarting supervisor.
            miner = subprocess.Popen([str(args.miner.resolve()), '--daemonized', '-c', str(root / 'miner.json')], cwd=root, stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            if miner.poll() is not None:
                raise RuntimeError('Miner exited early')
            info = rpc('getblockchaininfo', [])
            if info['blocks'] >= 3:
                stop(miner)
                mined = rpc('getblock', ['1', 0])
                raw = bytes.fromhex(mined)
                if raw[140] != 0 or raw[141] != 1 or raw[142:] != bytes.fromhex(template['coinbasetxn']['data']):
                    raise RuntimeError('Mined block changed the template coinbase or solution encoding')
                if rpc('getblocktemplate', [{}])['powversion'] != 2:
                    raise RuntimeError('Regtest did not activate v2 from height 2')
                logs = (root / 'miner-events.log').read_text()
                if not re.search(r'algo rx/zecnero height 1\b', logs):
                    raise RuntimeError('miner did not mine block 1 using v1')
                if not re.search(r'algo rx/zecnero2 height 2\b', logs):
                    raise RuntimeError('miner did not switch to v2 at block 2')
                print(f"PASS: {info['blocks']} PoW-validated Regtest blocks; live v1-to-v2 switch at block 2; coinbase preserved; mode={args.mode}")
                print(f'Logs: {root}')
                return
            time.sleep(0.25)
        raise RuntimeError('Miner did not produce three accepted blocks')
    finally:
        stop(miner)
        stop(node)
        print(f'Test artifacts: {root}')


if __name__ == '__main__':
    main()
