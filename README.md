# keel

**keel** is a lightweight, zero-dependency C++20 foundation library licensed under **0BSD**.

It complements the C++ standard library where `std` is heavy in compile time, Debug runtime overhead, or hidden allocations, while transparently accepting `std` types at API boundaries via non-owning `Ref` views.

## Design Principles

1. **Featherweight Headers**: Every public header (`include/keel/*.h`) is ~100–250 lines and transitively includes **only** `<cstddef>`, `<cstdint>`, and `<new>` (enforced in CI via `/showIncludes` and `-H`).
2. **Exception & RTTI Neutral**: Compiles cleanly with `-fno-exceptions -fno-rtti` (`/EHs-c- /GR-`), while remaining RAII-safe if user callbacks throw.
3. **Explicit Ownership & Allocation**: Move-only containers with explicit `.clone()`. Runtime `Allocator*` pointer stored in containers (no template bloat). First-class virtual-memory `Arena` and thread-local `get_scratch()` arenas.
4. **Debug Performance**: Flat data structures, shallow templates, no base-class chains, compiler builtin traits instead of `<type_traits>`.
5. **UTF-8 Everywhere**: All strings and filesystem paths are UTF-8 with `/` separators; UTF-16 conversion and `\\?\` long-path prefixes are hidden inside the Windows platform layer.

## Modules

| Header | Contents |
| :--- | :--- |
| `keel/core.h` | `i8`..`u64`, `f32`/`f64`, `isize`, `usize`, `KEEL_ASSERT`, builtin type traits, `move`/`forward` |
| `keel/bits.h` | `countl_zero`, `countr_zero`, `popcount`, `bit_width`, `bit_ceil`, `bit_floor`, `rotl`, `rotr`, `byteswap` |
| `keel/hash.h` | Fast 64-bit `hash_bytes`, `hash_u64`, `hash_combine`, `Hash<T>`, `Equal<T>` |
| `keel/memory.h` | `Allocator`, `default_allocator`, `set_oom_handler`, raw memory primitives |
| `keel/arena.h` | Virtual-memory `Arena` (with in-place top `realloc`), `ArenaTemp`, thread-local `get_scratch` |
| `keel/pool.h` | Generational slot `Pool<T>` and 64-bit `Handle<T>` |
| `keel/ref.h` | Non-owning `StringRef`, `ArrayRef<T>`, `MutArrayRef<T>` (accept `std` ranges without `#include`s) |
| `keel/error.h` | Value-type `Error`, `ErrorCode`, `ParseError`, `error_message` |
| `keel/result.h` | `Result<T, E>`, `KEEL_TRY`, `KEEL_TRY_ASSIGN` |
| `keel/optional.h` | Trivially-copyable `Optional<T>` and zero-size-overhead `SentinelOptional<T, Sentinel>` |
| `keel/function_ref.h` | 16-byte non-owning `FunctionRef<R(Args...)>` |
| `keel/function.h` | Move-only inline `Function<R(Args...), N>` with zero heap allocations |
| `keel/array.h` | Move-only dynamic `Array<T>` (`push_back`, `remove_at`, `swap_remove`, `clone`) |
| `keel/small_array.h` | `InplaceArray<T, N>` and small-buffer optimized `SmallArray<T, N>` |
| `keel/hash_map.h` | Flat open-addressed `HashMap<K, V>` with 1-byte metadata tags & heterogeneous lookup |
| `keel/hash_set.h` | Flat open-addressed `HashSet<K>` |
| `keel/string.h` | Move-only null-terminated UTF-8 `String` (`String::from`, `c_str()`, string builder ops) |
| `keel/utf8.h` | `utf8_validate`, `utf8_decode_next`, `utf8_encode`, `utf8_codepoint_count` |
| `keel/number.h` | Locale-independent integer & shortest round-trip IEEE-754 float/double parsing & formatting |
| `keel/format.h` | Type-safe printf-style `format`, `format_into`, `format_arena`, `format_buf`, `print`, `println` |
| `keel/fs.h` | UTF-8 path manipulation (`path_join`, `path_normalize`, etc.) and OS filesystem operations |
| `keel/io.h` | Move-only `File` with sticky write errors and console I/O |
| `keel/interop_std.h` | Opt-in conversions to `std::string`, `std::string_view`, `std::vector`, `std::span`, `std::optional` |

## Building & Running Tests

```bash
cmake -B build
cmake --build build
ctest --test-dir build -C Debug --output-on-failure
```

## License

Zero-Clause BSD ([0BSD](LICENSE)).
