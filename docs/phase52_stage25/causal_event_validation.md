# WinIOS — Phase 52, Stage 25: Causal Event Validation & Non-Duplication

## 1. Causal Separation Principles
A fundamental requirement for deterministic replay is that deterministic consequences must not be recorded as external inputs. Replaying deterministic consequences as external inputs creates duplicate mutations and state divergence.

### Audited Causal Relationships
1. **Interrupt Request vs Delivery**:
   - `ExternalInterruptAsserted`: Nondeterministic external signal assertion (Recorded).
   - `InterruptRouted`: Router queue insertion (Recorded).
   - `InterruptAccepted`: Hardware CPU acknowledgment (Recorded at exception boundary).
   - Internal GIC distributor register updates derived from CPU interface reads are **deterministic** and not recorded as independent inputs.
2. **Timer Comparator Expiration**:
   - Virtual time advances deterministically with instruction execution ($1\text{ ns}$ per instruction step).
   - When comparator matches virtual time, `TimerComparatorExpired` records the external deadline trigger.
   - The resulting PPI interrupt injection is evaluated deterministically by the GIC distributor.
3. **Exclusive Monitors**:
   - `StoreExclusiveResult` records whether the reservation was valid at the instant of execution ($0 = \text{success}, 1 = \text{failed}$).
   - The resulting cache-line invalidation to peer cores in `SmpExclusiveMonitorManager` is deterministic based on the write address and not re-injected.
