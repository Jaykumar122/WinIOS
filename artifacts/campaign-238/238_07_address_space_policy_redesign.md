# Stage 238.7: Address-Space Policy Redesign

## 1. Overview & Name-Based Policy Audit
In Campaign 237, the 2 GiB security sandbox boundary check was implemented via an ad-hoc name comparison in `UserAddressSpace::user_space_limit()`:
```cpp
return (name_ == "sec_uas") ? 0x0000000080000000ULL : kUserSpaceLimitVa;
```
This design was fragile:
1. It allowed security policy to be determined by an arbitrary mutable string name rather than an immutable type.
2. Renaming the address space or creating a sandbox with another name bypassed the 2 GiB boundary.

## 2. Immutable Policy Architecture
- Introduced explicit typed policy enum `AddressSpacePolicy`:
  ```cpp
  enum class AddressSpacePolicy : std::uint8_t {
      StandardGeneral = 0, // 32 GiB limit (0x0000000800000000ULL)
      StrictSandbox   = 1  // 2 GiB limit  (0x0000000080000000ULL)
  };
  ```
- Replaced name inspection with immutable members in `UserAddressSpace`:
  - `const AddressSpacePolicy policy_`
  - `const std::uint64_t user_space_limit_`
- Added constructor parameter `AddressSpacePolicy policy = AddressSpacePolicy::StandardGeneral`.
- Preserved backward compatibility: If `name == "sec_uas"`, constructor automatically maps to `StrictSandbox` (2 GiB limit).
- Once constructed, the policy and limit cannot be modified, preventing privilege elevation through name tampering.

## 3. Verification (`stage238_7_address_space_policy_tests`)
1. `StandardGeneral` instantiates with 32 GiB limit (`0x800000000ULL`) (100% PASS).
2. `StrictSandbox` instantiates with 2 GiB limit (`0x80000000ULL`) (100% PASS).
3. Legacy `"sec_uas"` name automatically maps to `StrictSandbox` (100% PASS).
4. Immutable invariants verified (100% PASS).
