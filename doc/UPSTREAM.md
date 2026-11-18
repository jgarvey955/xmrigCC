# Upstream synchronization

The 3.9.0 release incorporates these upstream development snapshots:

| Project | Branch | Commit |
| --- | --- | --- |
| [Bendr0id/xmrigCC](https://github.com/Bendr0id/xmrigCC) | dev | `e6e4c834d03d2b8df01cc99723978669bc6850f1` |
| [xmrig/xmrig](https://github.com/xmrig/xmrig) | dev | `20321c61b2e3cd735cbd3119bf7c9552ec18f268` |

Bendr0id's development branch includes the published master snapshot
`f899b498d470b1300ae8aaf6234bd347fe8ede49`. The XMRig snapshot adds changes
through November 14, 2026, including ARM64, TLS, SOCKS5 and Windows fixes.

The integration retains the local Zecnero and Salvium support, CC features,
request correlation and timeout handling, streaming RandomX commitments,
network liveness policy, stable AES dispatch and donation settings.
The MSVC build uses external hwloc, following the upstream dependency change.
