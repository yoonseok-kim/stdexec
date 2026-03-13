# AGENTS.md — stdexec

## Project Overview

stdexec is the reference implementation of C++26 `std::execution` (P2300), maintained by NVIDIA.
It is a **header-only** C++20 library. The primary build system is CMake (3.25+).
Supported compilers: GCC 11+, Clang 16+, Xcode 16+, MSVC 2022, nvc++ 25.9+.

## Build Commands

```bash
# Configure (Ninja recommended)
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Debug

# Build everything
cmake --build build

# Build a specific test target only
cmake --build build --target test.stdexec
cmake --build build --target test.exec
```

Key CMake options (all OFF by default unless noted):
`STDEXEC_BUILD_TESTS` (ON when top-level), `STDEXEC_BUILD_EXAMPLES` (ON by default),
`STDEXEC_ENABLE_CUDA`, `STDEXEC_ENABLE_TBB`, `STDEXEC_ENABLE_IO_URING`,
`STDEXEC_ENABLE_LIBDISPATCH` (auto on macOS).

## Test Commands

Test framework: **Catch2 v2** (tests auto-registered via `catch_discover_tests`).

```bash
# Run all tests
ctest --test-dir build --output-on-failure --timeout 60

# Run a single test by name regex
ctest --test-dir build -R "test_just"
ctest --test-dir build -R "test_sync_wait"

# Run test binary directly with Catch2 filters
./build/test/test.stdexec "[then]"          # filter by tag
./build/test/test.stdexec "sync_wait"       # filter by name substring
./build/test/test.stdexec --list-tests      # list all test cases

# Exclude tests matching pattern
ctest --test-dir build -E "nvexec"
```

Test targets: `test.stdexec` (core), `test.exec` (extensions), `test.nvexec` (GPU),
`test.tbbexec`, `test.asioexec`, `test.taskflowexec`, `test.scratch`.

## Lint / Format

```bash
# Check formatting (CI uses clang-format-21)
git ls-files '*.[ch]' '*.[ch]pp' '*.cu' '*.cuh' | xargs clang-format --dry-run --Werror

# Apply formatting
git ls-files '*.[ch]' '*.[ch]pp' '*.cu' '*.cuh' | xargs clang-format -i

# Static analysis (requires compile_commands.json)
clang-tidy -p build <source_file>
```

## Code Style Guidelines

### Formatting (.clang-format)

- **Indent**: 2 spaces, no tabs. **Column limit**: 100. **Line endings**: LF
- **Brace style**: Allman — opening brace on its own line for all constructs
- **Namespace indentation**: All contents indented inside namespaces
- **Pointer/reference alignment**: Left (`int* ptr`, `int& ref`)
- **Trailing comments**: 2 spaces before `//`

### Header Guards

Always `#pragma once`. Never use `#ifndef`/`#define` guards.

### License Header

Every file must start with the Apache 2.0 with LLVM Exceptions license block comment.

### Naming Conventions

| Element | Convention | Examples |
|---|---|---|
| Public types | `snake_case` | `run_loop`, `default_domain`, `env` |
| Tag types / CPOs | `snake_case_t` suffix | `set_value_t`, `connect_t` |
| Internal types | `__snake_case` (double `__` prefix) | `__sexpr`, `__opstate`, `__rcvr` |
| Template params | `_PascalCase` (single `_` prefix) | `_Sender`, `_Receiver`, `_Tag` |
| Concepts | `snake_case` (public), `__snake_case` (internal) | `sender`, `__callable` |
| Data members | `__name_` (double prefix + trailing `_`) | `__rcvr_`, `__state_` |
| Constexpr objects | `snake_case` (public), `__snake_case` (internal) | `set_value`, `__compose` |
| Macros | `STDEXEC_ALL_CAPS` | `STDEXEC_ATTRIBUTE(...)` |
| Type aliases / metafunctions | `_t` suffix / `__m` prefix | `env_of_t`, `__minvoke` |
| Diagnostic types | `_SCREAMING_CASE_SENTENCE_` | `_FUNCTION_MUST_RETURN_...` |

### Namespace Structure

```cpp
namespace STDEXEC            // macro expanding to `stdexec`
{
  namespace __detail { }     // shared internal details
  namespace __let { }        // per-algorithm implementation namespaces
}  // namespace STDEXEC

namespace experimental::execution { }  // extensions
namespace exec = experimental::execution;
```

Close every namespace with a comment: `} // namespace name`.

### Include Ordering

1. Relative project headers (`"__execution_fwd.hpp"`, `"__concepts.hpp"`)
2. Standard library headers (`<cstddef>`, `<utility>`, `<type_traits>`)
3. Use `""` for project headers, `<>` for standard headers
4. Use IWYU pragmas: `// IWYU pragma: export`, `// IWYU pragma: keep`

### Templates and Concepts

- Use `class` (not `typename`) in template parameter lists: `template <class _Ty>`
- `requires` clauses go on their own line, indented
- Prefer `static_cast<T&&>(x)` over `std::forward<T>(x)` and `std::move(x)`
- Place concept checks on **public interfaces only**; leave internal details unconstrained
- Use `static_assert` with descriptive messages for compile-time diagnostics

### Error Handling

Use the project macros instead of raw C++ constructs for cross-platform/CUDA portability:

| Instead of | Use |
|---|---|
| `try` | `STDEXEC_TRY` |
| `catch (T e)` | `STDEXEC_CATCH(T e)` |
| `catch (...)` | `STDEXEC_CATCH_ALL` |
| `throw expr` | `STDEXEC_THROW(expr)` |
| `std::terminate()` | `STDEXEC_TERMINATE()` |
| `assert(x)` | `STDEXEC_ASSERT(x)` |
| `__builtin_unreachable()` | `STDEXEC_UNREACHABLE()` |

### Attributes

Use `STDEXEC_ATTRIBUTE(...)` for portable attributes:
```cpp
STDEXEC_ATTRIBUTE(nodiscard, always_inline, host, device)
constexpr auto operator()(...) const noexcept -> ...
```

### Test Conventions

- Framework: Catch2 v2 (`#include <catch2/catch.hpp>`)
- Namespace alias: `namespace ex = STDEXEC;` at the top of test files
- Wrap test code in anonymous namespaces for internal linkage
- Use `STATIC_REQUIRE(...)` for compile-time checks, `REQUIRE(...)` for runtime
- Test helpers in `test/test_common/`: `receivers.hpp`, `senders.hpp`, `schedulers.hpp`
- Test file naming: `test_<feature>.cpp`
- Suppress warnings with `STDEXEC_PRAGMA_PUSH()` / `STDEXEC_PRAGMA_IGNORE_GNU(...)` / `STDEXEC_PRAGMA_POP()`

### Comment Style

- Section dividers: lines of `///...` characters
- Documentation: `//!` (Doxygen-style)
- Standard cross-references: `// [execution.senders.connect]`, `// [exec.let]`
- NOLINT annotations where clang-tidy would false-positive

## Maintainer Design Idioms

1. Store data (including downstream receivers) in the **operation state**; receivers hold only a pointer to it.
2. Assume schedulers and receivers are pointer-sized and cheap to copy — take them **by value**.
3. **Do not use `tag_invoke`** — it is deprecated.
4. Constrain `ThisSender<InnerSender>::connect(OuterReceiver)` by requiring `sender_to<InnerSender, ThisReceiver<OuterReceiver>>`.
5. Place concept checks on public interfaces only; do not recheck what was already validated.
