# Deterministic External Input Audit
## Audit of Guest-Visible Input Channels in Multi-Core Topologies (1..8 Cores)

---

## 1. Input Channel Classification & Mitigation

All external sources of entropy and nondeterminism across 1, 2, 4, and 8-core configurations were audited:

| Channel | Source | Vulnerability Risk | Resolution / Policy | Audit Verdict |
|---|---|---|---|---|
| Host Wall-Clock | `std::chrono`, `GetSystemTime` | Time variance between runs | Virtualized via `VirtualTime` (`CNTPCT_EL0`) | SECURE / PASS |
| Host Entropy | `std::random_device`, `/dev/urandom` | Divergent seed states | PRNG deterministic seed replayed via SNDL | SECURE / PASS |
| MMIO Reads | Peripheral device registers | Variable device responses | Recorded in SNDL v2 and replayed exactly | SECURE / PASS |
| Host OS Signals | Host thread preemption, signals | Unpredictable timing | Isolated: single host thread deterministic loop | SECURE / PASS |
| Asynchronous DMA | Host background threads | Out-of-band RAM writes | BLK-SMP-03 capability gate rejects DMA | SECURE / PASS |

---

## 2. Verification in Stage 28

Topology Scalability Test Group 8 verified that guest cores attempting direct host clock or unvirtualized input reads fail validation, preserving deterministic isolation across all 8 cores.
