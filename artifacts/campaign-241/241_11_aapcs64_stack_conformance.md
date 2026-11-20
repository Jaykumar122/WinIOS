# Stage 241.11: AAPCS64 and Stack Conformance

## 1. Calling Convention Validation
- **Register Parameters**: Arguments 1 through 8 passed in registers `X0` - `X7`.
- **Stack Spilling**: Arguments 9 through 18 pushed onto call stack in 8-byte aligned slots.
- **Stack Alignment**: 16-byte alignment maintained at all function entry points (`SP % 16 == 0`).
- **Callee-Saved Registers**: `X19` - `X28`, `X29` (frame pointer), `X30` (link register) preserved across nested and recursive calls.
- Conformance verified end-to-end via compiled workload `mem_func.macho`.
