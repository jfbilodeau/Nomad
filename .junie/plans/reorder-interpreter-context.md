---
sessionId: session-261121-152723-vift
---

# Requirements

### Current Implementation

**Header** (`include/nomad/interpreter/InterpreterContext.hpp`, 179 lines):
- Declares `InterpreterEvaluationGuard`, `InterpreterVariableInfo`, `LoopControl`, and `InterpreterContext`
- 14 methods are inlined (one-liners returning members or calling a single function)
- Methods are interleaved: loop control appears before constructors, result accessors are between receiver and diagnostics, evaluation guards are at the bottom of public section

**Source** (`src/nomad/interpreter/InterpreterContext.cpp`, 616 lines):
- Contains all non-inline method definitions
- Free helper functions in an anonymous namespace: `getScalarStorage()`, `validateValueStorage()`
- Definitions follow no particular order relative to the header

### Key Decisions

- **Grouping order**: Lifecycle → Core Accessors → Loop Control → Parameters → Receiver → Result → Diagnostics → Variables → Evaluation. This follows the natural usage pattern: construct → access context → manage execution state → work with data → evaluate.
- **Private methods**: `validateFrame()` and `resolveVariablePropertyStorage()` stay in the private section, ordered after the public declarations.
- **No signature changes**: Only reordering declarations and moving inline bodies to `.cpp`. No behavior changes.

### Proposed Changes

**`InterpreterContext.hpp`:**
1. Remove all inline method bodies, leaving only declarations
2. Reorder public declarations into the logical groups above
3. Keep private section unchanged (just `validateFrame()` and `resolveVariablePropertyStorage()`)

**`InterpreterContext.cpp`:**
1. Add definitions for the 14 methods previously inlined in the header
2. Reorder all definitions to match the new header order for consistency

# Delivery Steps

### ✓ Step 1: Reorder header declarations and remove inlines
Reorganize `InterpreterContext.hpp` declarations into logical groups and strip all inline bodies.

### * Step 2: Move inline definitions to .cpp and reorder
Add the moved definitions and reorder all definitions in `InterpreterContext.cpp` to match the new header order.