# Zecnero mining with XMRigCC

This fork supports Zecnero RandomX v1 (`rx/zecnero`) and v2 (`rx/zecnero2`, also
`rx2/zecnero`). The direct RPC client reads `powversion` from each block template
and switches automatically. Public Testnet uses v1 through block 999,999 and v2 from block 1,000,000.
V2 is experimental: the daemon must explicitly enable it on Regtest with
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

Use `algo: "rx/zecnero"` for the current public pool. Pool jobs choose the
algorithm; this miner also recognizes `rx/zecnero2` and `rx2/zecnero` jobs when
the server supports v2. The current official bridge release is verified here
for v1; a future v2 deployment needs a bridge/pool that advertises and validates
v2. The miner cannot make an older bridge understand a consensus upgrade.

From the repository root, copy the selected example, set your payout address
for a pool, and run it:

```sh
./build/xmrigDaemon --config config-zecnero-pool.example.json
# Or, with a synced node and its bridge already running:
./build/xmrigDaemon --config config-zecnero-bridge.example.json
```

Pool configurations do not use `daemon-cookie-*`. TLS for the pool uses `tls`
and optionally `tls-fingerprint`; the HTTPS cookie endpoint is a separate direct
RPC feature. For a bridge, set its `[node].cookie_path` explicitly to this node's
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
Use the correctly spelled `daemon-cookie-auth`.

| JSON key | Command-line option | Default and behavior |
| --- | --- | --- |
| `algo` | `-a`, `--algo` | Set `rx/zecnero` for v1 or `rx/zecnero2` / `rx2/zecnero` for v2. Direct RPC follows the node's `powversion` automatically, even if the configured version differs. Unknown versions are rejected. |
| `url` | `-o`, `--url` | Required node RPC host and port; this Testnet uses `127.0.0.1:18732`. On another machine, use the daemon's reachable hostname/IP. |
| `daemon` | `--daemon` | Default `false`; must be `true` for direct RPC solo mining. Requires an HTTP-enabled build. |
| `user` | `-u`, `--user` | Payout wallet address, sent in `mineraddress`. Default `x`, omitted, `null`, or empty uses `[mining].miner_address` on the daemon. An invalid explicit address fails; it never silently falls back. |
| `pass` | `-p`, `--pass` | Default `x`; ignored for RPC when a cookie file is configured. Otherwise the password paired with `daemon-rpc-user`. |
| `daemon-cookie-file` | `--daemon-cookie-file=PATH` | Default `null`. Path to read the RPC cookie. With a source, also the destination for automatically created/refreshed copies. Relative paths use the miner's working directory, typically `build/`. |
| `daemon-cookie-source` | `--daemon-cookie-source=SOURCE` | Default `null`. Use `https://HOST:18734` for authenticated remote retrieval, or a local path to copy the node's cookie. Requires `daemon-cookie-file`. Plain HTTP retrieval is rejected. With no source, the cookie must already exist at `daemon-cookie-file`. |
| `daemon-cookie-auth` | `--daemon-cookie-auth=USER:PASS` | Default `null`; required for HTTPS retrieval. One login matching an entry in the daemon's `zecnero-cookie-cred` array. `user,pass` is also accepted. This is independent of the payout wallet and RPC cookie credentials. |
| `daemon-cookie-fingerprint` | `--daemon-cookie-fingerprint=HEX` | Default `null`: save the TLS certificate SHA-256 fingerprint after the first successful authenticated retrieval in `<daemon-cookie-file>.fingerprint`; check it before sending credentials on later connections. Set 64 hex digits to preverify the first connection or explicitly approve certificate rotation. |
| `daemon-rpc-user` | `--daemon-rpc-user=USER` | Default `null`. Optional HTTP Basic RPC username paired with `pass` when no cookie file is used. The RPC server/proxy must support those credentials. Cookie authentication takes precedence. |
| `daemon-poll-interval` | `--daemon-poll-interval=N` | Default `1000` milliseconds; Zecnero clamps to at least 1000. Polls templates and retries while the node is syncing. |
| `daemon-job-timeout` | `--daemon-job-timeout=N` | Default `15000` milliseconds. Refreshes otherwise identical work after this interval (minimum 1000); not a network timeout. Changed work is applied immediately. |
| `tls` | `--tls` | Default `false`. Encrypts the separate node RPC connection, typically through a TLS proxy. HTTPS cookie retrieval uses TLS independently of this setting. |
| `tls-fingerprint` | `--tls-fingerprint=HEX` | Default `null`. Optional certificate pin for RPC TLS, separate from `daemon-cookie-fingerprint` and its saved cookie endpoint pin. |
| `enabled` | Config only | Default `true`; `false` disables this pool entry. |

The node's `zecnero-cookie-cred`, certificate/key paths, and `[mining].miner_address`
are daemon settings, not miner command-line options. Give each miner its own
`daemon-cookie-auth` login from the array; each can choose its own `user` wallet.
`daemon-zmq-port`, `nicehash`, `keepalive`, and `rig-id` do not control Zecnero's
direct RPC mining protocol.

Place a pool's command-line options after its `--url`. For example, from `build/`:

```sh
./xmrigDaemon --url NODE_ADDRESS:18732 --daemon --algo rx/zecnero \
  --user YOUR_ZECNERO_TESTNET_ADDRESS \
  --daemon-cookie-file ./.cookie \
  --daemon-cookie-source https://NODE_ADDRESS:18734 \
  --daemon-cookie-auth miner1:YOUR_PASSWORD
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

## Wallet and authentication for direct RPC

Choose one authentication mode per direct-RPC pool entry:

- **Local file only:** set `daemon-cookie-file` to the daemon-owned `.cookie`.
  Leave `daemon-cookie-source`, `daemon-cookie-auth`, and
  `daemon-cookie-fingerprint` unset or `null`. The miner rereads the file for
  every RPC, including after cookie rotation. It does not create or download it.
- **Authenticated HTTPS retrieval:** set `daemon-cookie-source` to the HTTPS
  endpoint and `daemon-cookie-auth` to its login. `daemon-cookie-file` is the
  miner's local destination; existing fingerprint verification and automatic
  refresh remain enabled. This requires the daemon's separate HTTPS feature.

A retrieval login receives the full RPC cookie, not a mining-only credential.
The hardened local-cookie daemon branch does not expose an HTTPS endpoint.
The HTTPS implementation is preserved in a separate branch for review.


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
  "daemon-cookie-file": "./.cookie",
  "daemon-cookie-source": "https://NODE_ADDRESS:18734",
  "daemon-cookie-auth": "miner1:YOUR_PASSWORD",
  "daemon-poll-interval": 1000,
  "daemon-job-timeout": 15000
}
```

The daemon config contains one TOML array under `[rpc]`:

```toml
zecnero-cookie-cred = ["miner1:YOUR_PASSWORD", "miner2:ANOTHER_PASSWORD"]
```

Configure the daemon's optional `[rpc.cookie_endpoint]` with `listen_addr`,
`cert_file`, and `key_file`. The node accepts each configured login independently.
After the first successful authenticated retrieval, the miner saves the certificate
fingerprint from the TLS connection to `<daemon-cookie-file>.fingerprint` with
owner-only permissions. Later connections verify this saved pin before sending
credentials, and reject a changed certificate. The saved record includes the host
and port; use a separate cookie file for each endpoint.

The initial connection uses trust on first use and must reach the intended daemon
on a trusted network. The login authenticates the miner, not the server. Set the
optional `daemon-cookie-fingerprint` to a verified 64-digit SHA-256 pin if you want
to authenticate the first connection in advance or approve a certificate rotation.
Failed logins never create a saved pin. A malformed pin file is rejected.
The miner retrieves and atomically creates its cookie file, and retrieves a fresh
one on reconnection, deletion, or HTTP 401. No shared filesystem is required.
The relative cookie path uses the process working directory; running from `build/`
creates `build/.cookie`. Failed retrieval never replaces a good cookie.

`user` remains the wallet; `daemon-cookie-auth` is the retrieval login. For HTTP
Basic RPC without cookies, use `daemon-rpc-user` and `pass`. Authentication fields
survive CC configuration serialization. Config files containing passwords should
be owner-readable only. Keep the separate HTTP RPC traffic on a trusted LAN/VPN
or use a TLS proxy (`tls` and `tls-fingerprint` configure RPC TLS).

Same-machine users may point `daemon-cookie-file` directly at the daemon's cookie,
or use a local path for `daemon-cookie-source`. The remote endpoint is not needed
for these modes. The local config has matching credentials for the daemon in `/home/jonathan/data/zecnerod/target/release/zecnero.conf`.

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
python3 tests/zecnero-sync.py build/xmrigMiner --https
python3 tests/zecnero-failover.py build/xmrigMiner
```

These cover authentication, sync pause/resume, cookie rotation, submission
handling and pool fallback/recovery. Only a null `submitblock` result counts as
acceptance. `WITH_ZECNERO_TESTS=ON` registers these Python checks with CTest;
it does not add another executable.

```sh
python3 tests/zecnero-regtest.py --node /path/to/zecnerod --miner build/xmrigMiner --mode light
python3 tests/zecnero-remote.py --node /path/to/zecnerod --miner build/xmrigMiner
```

These use isolated Regtest chains. The remote test covers TLS authentication,
multiple logins, failed pins/passwords, cookie creation/deletion/rotation, daemon
restarts, concurrent wallet requests, daemon-address fallback, wallet-only mining,
and fast-mode mining across v1 at block 1 to v2 from block 2 onward on opted-in
Regtest. The split node review branches assign no v2 activation height. The earlier
Regtest transition test requires the preserved experimental node, not the
local-cookie hardening branch.

## Consensus implementation provenance

The optimized v2 engine is ported from XMRig v6.26.0
`b2ca72480c58d197e18c885d9fc1a0c8d517e60a`. Its light-mode JIT addressing and
interpreter AES mixing are completed against the RandomX v2.0.1 reference.
Zecnero uses the reference hash directly; the extra Monero Stratum commitment
is not included. The reference vector generator uses native RandomX v2.0.1 at
`aaafe71322df6602c21a5c72937ac284724ae561` with salt `5a65636e65726f525801`.

Runtime tests were run on Linux x86-64. RISC-V v2 uses the portable interpreter;
its pre-existing v1 JIT remains available. ARM v2 light mode also uses the
interpreter. Other architectures need platform testing.
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
separate CMake build. Monero PR #10038's extra commitment and block-blob changes
are not part of Zecnero's 140-byte header protocol.

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
