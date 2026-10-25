# Zecnero mining with XMRigCC

This fork supports Zecnero RandomX v1 (`rx/zecnero`) and v2 (`rx/zecnero2`, also
`rx/zmr2` and the compatibility alias `rx2/zecnero`). The direct RPC client reads
`powversion` from each block template and switches automatically. The v2 plus
Orchard-only Testnet upgrade is not activating on the previously announced
date. Public activation will follow the exact height published by the Zecnero
developer; installing or restarting this miner does not activate it. The
current local node holds the Testnet selector at height 1,000,000 until that
final height is published. A daemon can exercise the transition on Regtest with
`[network.testnet_parameters] experimental_randomx_v2 = true`. That test chain
uses v1 for genesis and block 1, then v2 from block 2 onward. The miner follows this
transition without a restart, including when the seed stays unchanged.
The flag is a daemon setting; there is no second activation setting in the miner.
Both versions retain the Zecnero salt, header and seed schedule.

## Pool, bridge and direct RPC modes

The [official mining guide](https://zecnero.org/mine) describes the public pool
and the local Stratum bridge. XMRigCC supports both, as well as direct node RPC.

| Mode | Pool entry | Where the payout address goes |
| --- | --- | --- |
| Official pool, TLS | `stratum.zecnero.org:3334`, `daemon: false`, `tls: true`, `sni: true` | `user: "YOUR_ADDRESS.worker"`; the worker suffix is optional |
| Official pool, plain TCP | `stratum.zecnero.org:3333`, `daemon: false`, `tls: false` | Same address/worker login |
| Local bridge | `127.0.0.1:3333`, `daemon: false`, `tls: false` | The documented bridge uses the node's `mining.miner_address`; miner `user` is only a label |
| Direct node RPC | `127.0.0.1:18732`, `daemon: true` | Miner `user` overrides the node's address; empty/default `user` uses the node fallback |

Use `algo: "rx/zecnero"` as the pool fallback. Pool jobs choose the
algorithm; this miner also recognizes `rx/zecnero2` and `rx2/zecnero` jobs when
the server supports v2. A v2 bridge/pool must advertise the algorithm and verify
both the RandomX hash and its commitment. The miner cannot make an older bridge
understand a consensus upgrade.

From the repository root, copy the selected example, set your payout address
for a pool, and run it:

```sh
./build/xmrigDaemon --config config-zecnero-pool.example.json
# Or, with a synced node and its bridge already running:
./build/xmrigDaemon --config config-zecnero-bridge.example.json
```

Pool configurations do not use `daemon-cookie-*`. TLS for the pool uses `tls`
and optionally `tls-fingerprint`. Direct RPC reads a local cookie file. For a bridge, set its `[node].cookie_path` explicitly to this node's
actual cookie. The older official guide's upstream cache path is not this fork's
default.

Entries in `pools` are ordered for failover. Put the local daemon first and the
public pool second to use the pool if the primary connection exhausts its retries
or repeatedly reports that the daemon is syncing. Each `getblocktemplate` sync
reply counts toward `retries`; polling continues at `daemon-poll-interval`.
XMRigCC retries the primary and returns when it becomes usable. A secondary pool
does not split normal mining between both destinations. Always set `daemon` and
`tls` separately for each entry.

XMRigCC retains `xmrigDaemon`, `xmrigMiner`, and `xmrigServer` names. The official
installer manages its own `xmrig-zecnero`; it does not automatically manage these
XMRigCC executables. These commands work in Linux; Windows/WSL needs its own
build/runtime validation.

## Initialization choices

`randomx.init` is the number of dataset initialization threads. `init-avx2` is a
selector: `-1` auto, `0` disabled, `1` forced on supported CPUs. Positive `24`
also forces AVX2; it does not mean 24 threads. Auto disables that initializer on
Zen 4. Local forced-AVX2 tests produced invalid work; automatic selection passed.
This is a workaround while the forced path is investigated.

`cpu.argon2-impl: null` auto-selects cache initialization, including `AVX-512F`
on supported CPUs. `"AVX-512F"` can explicitly select it, but does not enable an
AVX-512 mining loop. It is independent of `init-avx2` and is already selected in
the Ryzen 7900X logs. Any additional v2 optimization must preserve exact hashes
and demonstrate a measured benefit.

## Run direct RPC mining

```sh
cd build
./xmrigDaemon --config ../config-zecnero-testnet.json
```

The Testnet example uses an explicit 24-thread CPU profile, two dataset initialization
threads, 5% donation, and disabled GPU/CC/API connections. Adjust the CPU profile
for your machine. `randomx.init` controls dataset construction, not mining threads.
For explicit thread limits, configure both `cpu.rx/zecnero` and `cpu.rx/zecnero2`.

## Command-line and JSON options for direct RPC

`./xmrigDaemon --help` and `./xmrigMiner --help` list the Zecnero options. The
following JSON keys belong to each entry in `pools`. Cookie options default to
`null` (disabled/unset); no login or node address is invented automatically.

| JSON key | Command-line option | Default and behavior |
| --- | --- | --- |
| `algo` | `-a`, `--algo` | Set `rx/zecnero` for v1 or `rx/zecnero2` / `rx2/zecnero` for v2. Direct RPC follows the node's `powversion` automatically, even if the configured version differs. Unknown versions are rejected. |
| `url` | `-o`, `--url` | Required node RPC host and port. The private full Testnet RPC is normally `127.0.0.1:18732`; the separate miner-only listener normally uses `HOST:18734`. |
| `daemon` | `--daemon` | Default `false`; must be `true` for direct RPC solo mining. Requires an HTTP-enabled build. |
| `user` | `-u`, `--user` | Payout wallet address, sent in `mineraddress`. Default `x`, omitted, `null`, or empty uses `[mining].miner_address` on the daemon. An invalid explicit address fails; it never silently falls back. |
| `pass` | `-p`, `--pass` | Default `x`. Pool/Stratum password; it is not reused for daemon authentication. |
| `daemon-cookie-file` | `--daemon-cookie-file=PATH` | Default `null`. Local full RPC: existing daemon-owned cookie. Remote miner RPC: destination for the client-specific cookie, securely created and atomically refreshed by XMRigCC. Relative paths use the miner's working directory, typically `build/`. |
| `daemon-cookie-source` | `--daemon-cookie-source=URL` | Default `null`. Exact `https://HOST:PORT/mining/cookie` URL on the same origin as `url`; enables client-specific remote cookie retrieval and renewal. |
| `daemon-cookie-fingerprint` | `--daemon-cookie-fingerprint=HEX` | Default `null`. Optional SHA-256 certificate pin used for both cookie bootstrap and miner RPC. Without a pin, normal CA-chain and hostname verification is required. |
| `rpc-login` | `--rpc-login=USER:PASS` | Default `null`. Basic daemon credentials and the TLS miner-cookie bootstrap login. It is separate from the payout wallet in `user`. |
| `daemon-poll-interval` | `--daemon-poll-interval=N` | Default `1000` milliseconds; Zecnero clamps to at least 1000. Polls templates and retries while the node is syncing. |
| `daemon-job-timeout` | `--daemon-job-timeout=N` | Default `15000` milliseconds. Refreshes otherwise identical work after this interval (minimum 1000); not a network timeout. Changed work is applied immediately. |
| `tls` | `--tls` | Default `false`. Required for the authenticated remote miner-cookie flow. Plain HTTP can instead use zecnerod's unauthenticated strict mining profile. |
| `tls-fingerprint` | `--tls-fingerprint=HEX` | Default `null`. Optional certificate pin for RPC TLS. |
| `enabled` | Config only | Default `true`; `false` disables this pool entry. |

`[mining].miner_address` is a daemon setting. Each miner can select its own
`user` payout address if the node supports it.
`daemon-zmq-port`, `nicehash`, `keepalive`, and `rig-id` do not control Zecnero's
direct RPC mining protocol.

Place a pool's command-line options after its `--url`. For example, from `build/`:

```sh
./xmrigDaemon --url NODE_ADDRESS:18732 --daemon --algo rx/zecnero \
  --user YOUR_ZECNERO_TESTNET_ADDRESS \
  --daemon-cookie-file /path/to/node/rpc/testnet/.cookie
```

Prefer an owner-readable config file for passwords instead of putting them in
shell history/process arguments. See `config-zecnero-testnet.example.json` for
the complete runnable configuration structure.

## Sync and RandomX readiness

The daemon withholds templates until peer tip discovery and pending block
verification finish. The miner reports `waiting for Zecnero daemon to finish
syncing; mining paused`, retries automatically, and pauses existing work if
the node returns to syncing. Regtest can mine without peers. A quiet Testnet
does not have to reach a height estimated from elapsed wall-clock time.

After the first usable template, `randomx init dataset` prepares memory;
`new job` lines are template notifications. Hashing starts after `dataset ready`
and CPU `READY`. Set mining threads with `cpu.rx/zecnero` and `cpu.rx/zecnero2`;
set dataset initialization threads separately with `randomx.init`. RandomX v2
uses `cpu.rx/zecnero2` when present, then the general `cpu.rx` or `cpu.*`
profile. To reuse the v1 thread list explicitly, set `"rx/zecnero2": "rx/zecnero"`
inside `cpu`.
`randomx.mode` selects `auto`, `fast` (full dataset), or `light` (cache only).
The published `v2x/rx-zecnero2` release gate permits `rx/zecnero2` only with a
full dataset and JIT, so use `auto` or `fast`. If the dataset or JIT is
unavailable, the miner reports the reason and waits for new work instead of
submitting from an unsupported path. This gate does not restrict v1.

For `rx/zecnero2`, the value tested against the target and sent as `result` is
Blake2b-256 of the 140-byte header followed by the RandomX v2 hash. The raw
RandomX hash is sent in the Stratum `commitment` field, matching `rx/2`.

## Wallet and authentication for direct RPC

XMRigCC supports two isolated cookie modes:

- Local full RPC (`127.0.0.1:18732`): `daemon-cookie-file` names the
  daemon-owned administrator cookie. XMRigCC only reads it and rereads it after
  rotation.
- Remote miner-only RPC (`HOST:18734`): `daemon-cookie-source` names the HTTPS
  `/mining/cookie` endpoint and `daemon-cookie-file` is a local destination.
  XMRigCC exchanges its individual bootstrap username/password for a
  short-lived `__miner__` cookie, renews it before expiry, and atomically updates
  the owner-only local file only when the value changes. The full RPC cookie is
  never retrieved or accepted on this listener.

Pool `user` is the payout wallet address. It is sent as `mineraddress` to the node,
which constructs the coinbase. Omit it, use an empty string, or the legacy `x`
default to use the daemon's configured mining address. Invalid addresses are
rejected. The miner requires the node's `mineraddress` capability for overrides.

```json
{
  "algo": "rx/zecnero",
  "url": "NODE_ADDRESS:18732",
  "daemon": true,
  "user": "YOUR_ZECNERO_TESTNET_ADDRESS",
  "pass": "x",
  "daemon-cookie-file": "/path/to/node/rpc/testnet/.cookie",
  "daemon-poll-interval": 1000,
  "daemon-job-timeout": 15000
}
```

For a remote miner-only connection:

```json
{
  "algo": "rx/zecnero",
  "url": "node.example:18734",
  "daemon": true,
  "tls": true,
  "user": "YOUR_ZECNERO_TESTNET_ADDRESS",
  "rpc-login": "rig-one:THE_RIG_ONE_BOOTSTRAP_PASSWORD",
  "daemon-cookie-source": "https://node.example:18734/mining/cookie",
  "daemon-cookie-file": "./secrets/rig-one.cookie",
  "daemon-cookie-fingerprint": "OPTIONAL_64_HEX_SHA256_CERTIFICATE_PIN"
}
```

The cookie endpoint must match the configured RPC host and port exactly. Cookie
bootstrap and subsequent authenticated RPC calls both require verified TLS: use
a publicly trusted certificate with a matching hostname, or configure the exact
certificate pin. Bootstrap credentials and miner cookies are never sent over
plain HTTP. Plain HTTP on the miner port remains usable only through zecnerod's
unauthenticated strict method profile.

## Build

Use the standard `build/` directory and the source checkout being edited:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DXMRIG_DEPS=scripts/deps \
  -DBUILD_STATIC=ON -DWITH_ZLIB=ON -DWITH_OPENCL=OFF -DWITH_CUDA=OFF \
  -DWITH_RANDOMX=ON -DWITH_HTTP=ON -DWITH_ZECNERO_TESTS=OFF
cmake --build build -j 6
```

The release build produces `xmrigDaemon`, `xmrigMiner`, and `xmrigServer`.
There is no separate Zecnero test executable. Optional Python integration checks
run against the normal `build/xmrigMiner` binary:

```sh
python3 tests/zecnero-rpc.py build/xmrigMiner
python3 tests/zecnero-sync.py build/xmrigMiner
python3 tests/zecnero-failover.py build/xmrigMiner
```

These cover authentication, sync pause/resume, cookie rotation, submission
handling and pool fallback/recovery. Only a null `submitblock` result counts as
acceptance. `WITH_ZECNERO_TESTS=ON` registers these Python checks with CTest;
it does not add another executable.

```sh
python3 tests/zecnero-regtest.py --node /path/to/zecnerod --miner build/xmrigMiner --mode fast
```

These use isolated Regtest chains. The remote test covers TLS authentication,
multiple logins, failed pins/passwords, cookie creation/deletion/rotation, daemon
restarts, concurrent wallet requests, daemon-address fallback, wallet-only mining,
and fast-mode mining across v1 at block 1 to v2 from block 2 onward on opted-in
Regtest. It requires a release node implementing the v2 commitment rule. The
older experimental node in `zectesting` compares the raw RandomX hash and is not
a launch-compatible v2 fixture.

## Consensus implementation provenance

The Zecnero integration is ported directly from `xmrig-zecnero` branch
`v2x/rx-zecnero2` at `67bd807eebc67d16b7c8d0b2c69be0489cb07e2b`.
It inherits the upstream RandomX v2 configuration and changes only the Argon2
salt to `ZecneroRX\x01`; it also carries that branch's full-dataset/JIT release
gate, light/interpreter completion, VM-pool bound fix, and test hooks. The old
`rx2/zecnero` spelling is only a compatibility alias for `rx/zecnero2`.

Vectors are from `randomx-vectors` branch `v2f/v2-vectors` at
`12fa94a9ada546da2aac9febbb6f699bc1b5f4dc`. Zecnero v2 uses the same commitment
construction and Stratum field convention as Monero `rx/2`. Release workers
intentionally use only the upstream-supported fast/JIT path; the completed
light/interpreter code remains present for upstream validation builds.
Original licenses and attribution, external algorithm names, and existing binary
and control-protocol identifiers are preserved.

### Upstream maintenance review (2026-09-27)

RandomX reference fixes through `7607fb2faed24d5a679e139a9828d194bbc644a4`
were reviewed against the v2.0.1 baseline. The applicable ELF assembly changes
are backported: JIT template bytes are readable in `.rodata` on execute-only
systems, callable reciprocal code remains in `.text`, and ELF assembly sources
mark the stack non-executable. The upstream x86 dataset-size fix is already
covered here by `emit()`, which advances by the selected template's actual size.
Upstream CMake changes belong to the daemon's reference library, not this miner's
separate CMake build. The commitment does not change Zecnero's 140-byte header;
it changes the v2 proof value and adds the underlying hash to Stratum submit.

Sources: [RandomX post-tag fixes](https://github.com/tevador/RandomX/compare/v2.0.1...7607fb2faed24d5a679e139a9828d194bbc644a4),
[Monero v2 integration](https://github.com/monero-project/monero/pull/10038).

## Pool and failover verification (2026-09-27)

The four active local XMRigCC builds were checked against the endpoints above.
Each submitted an accepted share on both public pool ports with no rejected
shares during those short runs. Each also mined three node-validated v1 blocks
through the official testnet-v0.1.2 bridge (commit
`f7731669c12712dc94f7090664f251db16143190`) on isolated Regtest.

The sync-failover regression covers an initially syncing primary, continued RPC
polling while Stratum is active, return to the recovered primary, and another
sync/fallback/recovery cycle. Reproduce it from this repository root:

```sh
python3 tests/zecnero-failover.py build/xmrigMiner
```

All four builds also passed the existing RPC, sync and HTTPS-rotation tests,
and direct Regtest mining across the v1-to-v2 transition. No long-duration pool
payout or Windows/WSL result is implied by these Linux compatibility checks.
