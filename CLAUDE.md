# Programming mindset

- Think before coding. Design for the simplest solution that solves the real problem: the "super idea" that fixes safety, performance and readability at once.
- Do not add features, options or abstractions that were not asked for. Do it right the first time; avoid technical debt.
- Zero dependencies beyond the C standard library, raylib and raygui. Do not add others without a strong reason.
- Always motivate, always say why. Never leave a decision without a reason in a comment.

## Performance

- Think about performance from the start, in the design phase, because it is hardest to fix later. Do back-of-the-envelope sketches for the four resources (network, disk, memory, CPU) and their two characteristics (bandwidth, latency).
- Optimize for the slowest resource first, in the order network, disk, memory, CPU, adjusted for frequency of use.
- Be explicit. Do not depend on the compiler to optimize something that matters; extract hot loops into standalone functions with primitive arguments and no `self` pointer, so that the compiler (and a reader) can see that nothing is aliased.
- Batch work. Amortize costs by processing many items at once rather than one at a time.
- Avoid dynamic allocation after initialization. Allocate memory statically or once at startup, with fixed upper bounds, and never call `malloc` or `free` in the main loop.
  - This eliminates use-after-free and avoids fragmentation.

# C Programming Style Guide

## General

- Run no formatter that changes these rules.

## Indentation, lines and spacing

- Use 4 spaces for indentation, never tabs.
- Keep lines to at most 100 columns, with no exceptions for long strings other than URLs.
- Always put a space after keywords (`if (`, `for (`, `while (`), but not after function names (`assert(x)`).

## Imports

- Include the project's own headers as `<zeman/name.h>`: headers live in `include/zeman/` of `shared/`, `desktop/` or `android/`, never beside the .c files.

## File layout

- Use the order: includes (standard library, then third-party, then project), then macros and constants, then types, then function declarations, then definitions. Put `main` at the top of the file when it is the entry point, and the most important functions first.

## Comments and documentation

- Use `/**` to start a documentation comment. Example:

```c
/**
 * This is an example documentation comment.
 * It is split on multiple lines.
 */
```

- Use `//` for line comments. Comments are complete sentences, with a capital letter, a space after the slashes, and a full stop. Write inline comments (after code) as phrases and omit the full stop.
- Always say why. The code shows how; the comment explains the reason, the trade-off, or the thing that is not obvious.
- Explain the "why" for every non-obvious choice, including the choice of a bound, a magic number, or an unusual order of operations.
- Keep documentation, comments and code in sync, and update comments when the code changes.

## Naming

- Use `snake_case` for functions, variables, parameters and file names
- Use Ada case for types (`Header_Flags`, `Task_Completion`: each word capitalized, words joined by underscores; a one-word type is just capitalized, like `Event`).
- Use `SCREAMING_SNAKE_CASE` for macros and constants. The rule is for types we define, not for raylib, raygui, SQLite or libc types.
- Do not abbreviate names (except for loop indexes `i`, `j`, and widely understood terms such as `x`, `y`).
- Use proper capitalization for acronyms (`HTTP_Response`, not `Http_Response`) where they appear in type names.
- Add units or qualifiers to variable names, and put them last, so that related names sort together and read in order of significance: `latency_ms_max` rather than `max_latency_ms`; `width_pixels`, `cell_height_pixels`.
- Name things so that related names have the same length where it helps alignment, for example `source` and `target` rather than `src` and `dest`.
- Use nouns for types and variables, and verbs for functions. Name a function for what it does; callbacks and predicates read as questions (`is_leap_year`).
- Distinguish between an index (0-based), a count (1-based, the number of things) and a size (count times unit): `index`, `count`, `size_bytes`. Use `index < count` and `index <= count_max`.
- Prefix helper functions with the name of the function that calls them, when they are used in only one place: `draw_grid` and `draw_grid_cell`. Callbacks go last in the parameter list.
- Struct and type names are in the order of the most significant part first.
- Do not overload names with different meanings. Do not use the name `data`, `info`, `value` or `tmp` when a precise name exists.

## Variables

- Initialize every variable at declaration.
- Declare variables at the smallest possible scope, and as close as possible to where they are used, to reduce the chance of use before initialization.

## Constants

- Use `static const` or `enum` for constants rather than `#define` where possible. Use `#define` only for macros that must be macros. Give every constant a unit or comment.

## Globals

- Do not use global mutable state. Pass state explicitly through a context struct.

## Scopes

- Function statements should have their beginning curly brace on the same line as the function signature.
- If statements should have their beginning curly brace on the same line as the condition.
- Loops (`for`, `while`, `do`) and `switch` follow the same curly brace placement.

## Data types

- Use explicitly sized types (`int32_t`, `uint32_t`, `uint64_t`, `size_t` only for sizes and indexes of memory). Avoid `int`, `long` and `unsigned` where the range matters.
  - Where an API (raylib, raygui, libc) forces `int` or `float`, convert at the boundary.
- Use `bool` for booleans, never `int`.

## Conditional statements

- If statements must have their bodies in curly brackets. No single line if statement body without curly brackets.
- Split compound conditions into nested `if` statements where that makes each branch clear. Prefer positive conditions (`if (index < length)`) over negations.
- State invariants positively. For `else` branches, make sure the case is handled or deliberately asserted unreachable.
- Handle every case in a `switch` explicitly. Do not add a `default:` that hides a missing case; assert unreachable instead.
- Do not use of ternary operator unless mandatory.

## Control flow

- Use only simple, explicit control flow. Do not use `goto` (other than a single cleanup label where unavoidable), `setjmp`/`longjmp`, or recursion. All executions that should be bounded must be bounded.
- Put a fixed upper bound on every loop and every queue or buffer, so that a runaway loop cannot occur. A loop that must run forever, such as the main loop, should be the only exception and should say so.

## Functions

- Keep functions to at most 70 lines. If a function grows longer, split it; centralize control flow in the parent function and move pure logic into helpers.
- Good function shape is a few parameters and a simple return type. Prefer passing structs by `const` pointer when large, and by value when small.
- Mark everything that can be `static` as `static`. Mark pointer parameters `const` when the function does not modify the data.
- Keep control flow (`if`, `switch`, loops) in the parent function and pure computation (no I/O, no side effects) in helpers. Keep leaf functions pure.
- Do not duplicate or alias variables or state, since duplication creates opportunities for them to drift out of sync.
- Avoid out parameters where a return value will do; if you must use them, assert on them.

## Structs

- Put a declaration of each struct field on its own line, and order fields from largest to smallest alignment so that padding is minimal. Assert the struct size with `static_assert` when layout matters.

## Assertions

- Use `assert` (from `<assert.h>`) liberally to check function arguments, return values, pre- and post-conditions, and invariants. Aim for at least two assertions per function on average.
- Assert the positive space (what you expect) and the negative space (what you do not expect). The boundary between valid and invalid data is where bugs live.
- Assertions detect programmer errors and must crash. Never use them for expected, recoverable conditions such as I/O failure; handle those by other means.
- Pair assertions: check important properties at two different code paths where possible, for example when writing and when reading.
- Split compound assertions: `assert(a); assert(b);` rather than `assert(a && b);`.
- Use `static_assert` (C11 `_Static_assert`) to check compile-time constants, sizes and relationships between them.
- Assertions must not have side effects, because they may be compiled out with `NDEBUG`.

## Error handling

- All errors must be handled. Check every return value that can fail; do not ignore results. Do not leave `(void)` casts on fallible calls without a comment explaining why.
- Compile with all warnings on (`-Wall -Wextra`, `/W4` on MSVC) and treat warnings as errors in our own code. Fix the cause rather than suppressing the warning.

## Type casting

- Do not use implicit conversions that lose information. Be explicit with casts, and keep them rare.

## Preprocessor macros

- Avoid complex macros. If a macro has side effects or evaluates its argument more than once, it must be named in capitals and commented.

## Undefined behavior

- Do not rely on undefined behavior. Be careful with signed overflow, shifts, strict aliasing, uninitialized reads and pointer arithmetic. Use unsigned types for wraparound arithmetic.

# Other project management

## Version control

- Write descriptive commit messages that explain the reason for the change, not only what changed.

## Dependencies and tooling

- Avoid global dependencies, keep them to a minimum.
- Keep the build in CMake, and keep tooling small and consistent. Prefer a shell script or CMake target over adding a new language or tool.
- Keep raylib and raygui local to the project in `external/`.
