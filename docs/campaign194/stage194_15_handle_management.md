# Campaign 194 Stage 194.15: Kernel Object & Handle Management

## Objective
Provide capability-secure, unified object management for the synthetic kernel runtime:
1. `KernelObject`:
   - Unified representation of kernel-managed entities: `Thread`, `IpcPort`, `MemoryRegion`, `Device`, `SyncPrimitive`.
   - Reference counting (`add_ref`, `release`, `ref_count`).
   - Capability rights bitmask: `Read`, `Write`, `Execute`, `Transfer`, `Duplicate`, `Destroy`.
2. `HandleTable`:
   - Unified handle table mapping integer handles (1..N) to objects + granted rights.
   - `allocate_handle(object, rights)`: Assigns a handle with granted rights.
   - `get_object(handle, required_rights)`: Verifies permissions and retrieves object pointer.
   - `duplicate_handle(handle, rights_mask)`: Capability attenuation (cannot elevate rights, requires `Duplicate` right).
   - `close_handle(handle)`: Decrements refcount, reclaims handle slot.

## Implementation Details
- Created `src/kernel/handle_table.h` and `src/kernel/handle_table.cpp`:
  - `KernelObjectType` and `HandleRights` namespaces.
  - `KernelObject` base class.
  - `HandleTable` with free list index reuse, maximum capacity bound enforcement, and capability attenuation logic.
- Authored test suite `tests/unit/stage194_15_handle_management_tests.cpp`.

## Test Results
- `stage194_15_handle_management_tests`:
  - `test_handle_allocation`: PASSED (handle creation, validation, type query).
  - `test_handle_rights_enforcement`: PASSED (read/write permitted, execute/destroy denied).
  - `test_handle_capability_attenuation`: PASSED (duplicated handles receive attenuated intersection of rights; elevation prohibited; duplication without duplicate right fails).
  - `test_handle_lifetime_and_refcounting`: PASSED (correct increment and decrement of refcount on allocation/closing).
  - `test_handle_slot_reuse`: PASSED (free-list slot reuse on closed handles).
  - `test_handle_table_capacity_exhaustion`: PASSED (strict adherence to table maximum capacity).
  - Total: 43 passed, 0 failed.

## Evidence Classification
- Capability Management: `SYNTHETIC_KERNEL`
