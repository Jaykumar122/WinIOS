# WinIOS Phase 52 Stage 26 — SNDL v2 Event Cursor Specification

## 1. Event Cursor Design & Semantics

The `SmpReplayEventCursor` (`src/replay/smp_replay_cursor.h`) provides the core abstraction through which the replay coordinator observes, validates, and consumes the serialized SNDL v2 event stream. It wraps `SndlV2ReplayReader` and guarantees atomic sequence validation.

```
                    +------------------------------------+
                    |        SndlV2ReplayReader          |
                    +------------------------------------+
                                      |
                             read_next() / peek_next()
                                      v
                    +------------------------------------+
                    |        SmpReplayEventCursor        |
                    |------------------------------------|
                    | current_sequence_                  |
                    | consumed_count_                    |
                    | strict_monotonicity_ = true        |
                    +------------------------------------+
                        /             |             \
                       /              |              \
             peek()                   |            consume_expected_type()
         (non-destructive)            |            (atomic validation & advance)
                                      v
                                consume()
                         (strict record parity)
```

### 1.1 State Distinctions in Event Lifecycle

| Concept | API Method | Description | Cursor Advanced? |
|---|---|---|---|
| **Observed** | `peek(rec)` | Record inspected from buffer without state mutation. | No |
| **Expected** | `peek_type_is(type)` | Evaluates whether the next record matches a target event type. | No |
| **Validated** | `validate_sequence_continuity(rec)` | Ensures $S_{next} == S_{curr} + 1$ (or initial sequence). | No |
| **Consumed** | `consume_expected_type(type, rec)` | Validates type and sequence, then advances cursor by exactly 1. | Yes |
| **Applied** | (Coordinator method) | Event injected into guest state (e.g. interrupt router, CPU). | (Post-cursor advance) |

---

## 2. Sequence Continuity & Error Handling

Under `strict_monotonicity = true`:
1. **Initial Event:** The first record must have $S_{0} \ge \text{header.initial\_global\_sequence}$.
2. **Subsequent Events:** Each subsequent record must strictly obey:
   $$S_{i+1} = S_i + 1$$
3. **Gap Detection:** If $S_{i+1} > S_i + 1$, the cursor immediately rejects the read with `SndlV2ErrorCode::InvalidSequence` ("Sequence gap detected: expected X, got Y").
4. **Monotonicity Violation:** If $S_{i+1} \le S_i$, the cursor rejects the read with `SndlV2ErrorCode::InvalidSequence`.
5. **Atomic Rejection:** If `consume_expected_type()` fails because the event type does not match or sequence validation fails, the reader is **not** advanced. The cursor position remains stable, allowing the coordinator to extract exact divergence diagnostics.
