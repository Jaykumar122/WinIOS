# WinIOS Campaign 225 — Stage 225.14 & 225.15 Virtual Network Device & Loopback Subsystem

## 1. Network Subsystem Design & Security Policy
The `VirtualNetworkService` ([src/devices/network_service.h](file:///D:/WinISO26/WinIOS/src/devices/network_service.h), [src/devices/network_service.cpp](file:///D:/WinISO26/WinIOS/src/devices/network_service.cpp)) provides an isolated, deterministic packet-based networking environment for guest applications.

### Host Isolation Guarantee:
In compliance with project safety invariants, the virtual network device is **strictly isolated from host network adapters, sockets, and the internet**. No raw IP packets or host socket connections leave the WinIOS sandbox. Guest networking operates entirely through an in-memory loopback and peer queue.

---

## 2. Virtual Ethernet Interface & Queuing Model
- **Interface Name:** `vnet0`
- **MAC Address:** `02:00:00:00:00:01` (locally administered unicast)
- **MTU (Maximum Transmission Unit):** $1514$ bytes (standard Ethernet frame including header and payload).
- **Queue Bounds:**
  - Receive Queue Capacity: $64$ packets.
  - Transmission Queue Capacity: $64$ packets.
- **Packet Drop Policy:** When a queue reaches its capacity of 64 packets, subsequent packets are dropped deterministically and `rx_dropped_` / `tx_dropped_` counters are incremented.

---

## 3. Syscall API & Packet Echo Dispatch
Guest processes interact with the virtual network through Category C ABI v1.9 syscalls:
- Syscall 554 (`SYS_WINIOS_NET_SEND`): Transmits a raw packet payload into the virtual network.
  ```c
  int32_t winios_net_send(const void* buffer, size_t length);
  ```
- Syscall 555 (`SYS_WINIOS_NET_RECV`): Dequeues the next received packet into a user-provided buffer.
  ```c
  int64_t winios_net_recv(void* buffer, size_t max_len, size_t* out_len);
  ```
- Syscall 556 (`SYS_WINIOS_NET_STATUS`): Retrieves interface operational status and telemetry counters.
  ```c
  int32_t winios_net_status(winios_net_status_t* out_status);
  ```

### Loopback Echo Engine:
When loopback mode is active (default), packets sent via `winios_net_send` are mirrored directly into the receive queue, allowing guest diagnostic workloads (such as `campaign225_system_monitor`) to verify IPC and network stack integrity without host socket exposure.

---

## 4. Verification Evidence
- [tests/unit/stage225_14_network_service_tests.cpp](file:///D:/WinISO26/WinIOS/tests/unit/stage225_14_network_service_tests.cpp): Validates packet transmission, MTU bounding ($> 1514$ rejected), loopback reception, queue overflow drops, and network statistics telemetry.
