# WinIOS Campaign 225 — Stage 225.18 Virtual Notification Service

## 1. Notification Subsystem Architecture
The `VirtualNotificationService` ([src/devices/notification_service.h](file:///D:/WinISO26/WinIOS/src/devices/notification_service.h), [src/devices/notification_service.cpp](file:///D:/WinISO26/WinIOS/src/devices/notification_service.cpp)) provides an Apple UserNotifications-inspired dispatch framework for guest applications to present toast alerts, system badges, and operational banners.

---

## 2. Notification Model & Capacity Limits
Notifications are stored in a thread-safe, bounded ring buffer to prevent malicious or runaway applications from exhausting system memory.

### Capacity Guarantees:
- **Maximum Active Notifications:** $64$ concurrent alerts.
- **Ring Buffer Eviction:** If the buffer is full when a new notification is posted, the oldest notification is dismissed automatically (`kMaxCapacity = 64`).
- **Maximum Text Bounds:**
  - `title`: Capped at $64$ characters.
  - `body`: Capped at $256$ characters.
  - `category`: Capped at $32$ characters (`"system"`, `"alert"`, `"hardware"`, `"app"`).

### Notification Item Descriptor:
```cpp
struct NotificationItem {
    uint32_t id;                // Monotonic notification ID
    uint32_t sender_pid;        // PID of sending application
    std::string title;          // Alert title
    std::string body;           // Alert message
    std::string category;       // Category classification
    uint64_t timestamp_us;      // Timestamp posted (microseconds)
    uint32_t duration_ms;       // Visibility duration (default: 3000 ms)
    bool     active;            // True while active, false when dismissed
};
```

---

## 3. Syscall API Dispatch
- Syscall 557 (`SYS_WINIOS_NOTIFICATION_POST`): Posts a new alert.
  ```c
  uint32_t winios_notification_post(const char* title, const char* body, const char* category, uint32_t duration_ms);
  ```
- Syscall 558 (`SYS_WINIOS_NOTIFICATION_DISMISS`): Manually dismisses an active notification by ID.
  ```c
  int32_t winios_notification_dismiss(uint32_t notif_id);
  ```

---

## 4. Verification Evidence
- [tests/unit/stage225_18_notification_tests.cpp](file:///D:/WinISO26/WinIOS/tests/unit/stage225_18_notification_tests.cpp): Validates notification creation, string truncation safety, ring buffer wrapping at 64 items, expiration timing, and explicit dismissal.
