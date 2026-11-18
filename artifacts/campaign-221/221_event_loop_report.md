# WinIOS Guest Event Loop & Timer Integration Report

**Campaign:** 221 (Stage 221.12)  
**Subsystem:** Event Dispatching, Input Delivery & Virtual Timers  

---

## 1. Event Polling & Dispatch

Guest applications interact with the event subsystem through `<winios/ui/event.h>`:
- `winios_poll_event(winios_event_t* out_event)`: Non-blocking dequeue of the next pending event. Returns 0 if an event was retrieved, or negative if the queue is empty.
- `winios_wait_event(winios_event_t* out_event, uint32_t timeout_ms)`: Bounded wait for incoming events.
- **Event Types:**
  - Touch: `TOUCH_BEGIN`, `TOUCH_MOVE`, `TOUCH_END`, `TOUCH_CANCEL`
  - Keyboard: `KEY_DOWN`, `KEY_UP`, `KEY_CHAR`
  - Timer: `EVENT_TIMER`
  - Window Lifecycle: `WINDOW_FOCUS`, `WINDOW_DEFOCUS`, `WINDOW_CLOSE_REQUEST`
  - IPC: `IPC_INCOMING`

---

## 2. Timer Architecture

- `winios_create_timer(interval_ms)`: Registers a recurring or one-shot timer tied to the calling thread. Fires `WINIOS_EVENT_TIMER` when elapsed.
- **Teardown Safety:** All active timers registered by an application are automatically cancelled upon process termination.
