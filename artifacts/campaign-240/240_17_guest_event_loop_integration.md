# Stage 240.17: Guest Event-Loop Integration

## 1. Event Loop Mechanics
1. **Event Dispatch & Queuing**:
   - Input and lifecycle events enqueued into task event queue via `InputRouter`.
   - Guest queries events via polling syscall or blocks on event arrival.
2. **Queue Limits**:
   - Enforces 256 pending event capacity limit per process to prevent resource exhaustion attacks.
   - Overflow events dropped or reported gracefully.
