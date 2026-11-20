# Stage 238.8: Guest Memory Validator Audit

## 1. Overview & Policy Enforcement Gap
In Campaign 237, `GuestMemoryValidator::copy_string` enforced string bounds checks against a hardcoded constant `kUserSpaceLimitVa` (32 GiB / `0x0000000800000000ULL`):
```cpp
if (guest_va >= kUserSpaceLimitVa) return false;
```
When an application was configured with a strict 2 GiB security sandbox (`AddressSpacePolicy::StrictSandbox`), `copy_string` permitted guest string pointers between 2 GiB and 32 GiB, violating address space confinement before page-table checks were invoked.

## 2. Hardened Implementation
`GuestMemoryValidator::copy_string` in `src/memory/guest_memory_validator.cpp` was updated to dynamically respect the authoritative address space limit of the target `UserAddressSpace`:
```cpp
const std::uint64_t limit_va = uas.user_space_limit();
if (guest_va >= limit_va) {
    return false;
}
```
Furthermore, boundary checks verify `(guest_va + len) <= limit_va` to prevent integer wrap-around and partial copy-in overflows.

## 3. Verification & Coverage
1. Under `AddressSpacePolicy::StandardGeneral` (32 GiB limit):
   - Access at `0x100000000ULL` (4 GiB) is permitted if mapped.
   - Access at `>= 0x800000000ULL` (32 GiB) is strictly rejected (`EFAULT`).
2. Under `AddressSpacePolicy::StrictSandbox` (2 GiB limit):
   - Access at `0x40000000ULL` (1 GiB) is permitted if mapped.
   - Access at `0x80000000ULL` (2 GiB) and higher is strictly rejected (`EFAULT`).
   - Access spanning across 2 GiB boundary (e.g. `0x7FFFFFF0ULL` with length 32) is cleanly rejected without buffer overflow.
3. Verified comprehensively via `stage238_7_address_space_policy_tests` (100% PASS).
