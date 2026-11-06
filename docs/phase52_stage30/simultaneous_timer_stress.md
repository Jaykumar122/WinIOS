# Simultaneous Timer Expirations & Deterministic Ordering

## 1. Concurrent Timer Scenario
To stress test concurrent timer handling:
- Virtual timers on all 8 virtual cores were configured to expire simultaneously at virtual timestamp $T_v = 1,000,000$.
- Each expiration generated a Private Peripheral Interrupt (PPI, INTID 27) targeted at its local core.

## 2. Global Event Serialization
Because multiple timers expired at the identical virtual timestamp, determinism required strict, non-ambiguous tie-breaking:
- Tie-breaking rule: Ordered monotonically by core ID ($Core_0 \to Core_1 \to \dots \to Core_7$).
- Recorded sequence numbers: $E_{seq} = [101, 102, 103, 104, 105, 106, 107, 108]$.

## 3. Repeated Execution Results (10 Runs)
The combined interrupt storm and simultaneous timer workload was replayed 10 consecutive times:
- Run 1 Final Hash: Bit-identical to baseline.
- Run 2 Final Hash: Bit-identical to baseline.
- Run 3 Final Hash: Bit-identical to baseline.
- Run 4 Final Hash: Bit-identical to baseline.
- Run 5 Final Hash: Bit-identical to baseline.
- Run 6 Final Hash: Bit-identical to baseline.
- Run 7 Final Hash: Bit-identical to baseline.
- Run 8 Final Hash: Bit-identical to baseline.
- Run 9 Final Hash: Bit-identical to baseline.
- Run 10 Final Hash: Bit-identical to baseline.

Conclusion: 10/10 repeated runs produced identical final state. Simultaneous timer expirations are completely deterministic.
