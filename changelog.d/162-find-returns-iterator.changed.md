- **Breaking (source):** `find` returns an iterator on `flat_map`, `flat_set`, `static_unordered_map` and `static_unordered_set`, or `end()` for an absent key, as std's containers do (#162). `s.find(k) != s.end()` now answers correctly on all four. On `flat_set` it used to be true for every key, because `find` returned `nullptr` and the iterator was a pointer (#641). The pointer form is now `find_value`, on the two maps. `find_iterator` is gone; `find` is that function now. `lookup_table` has no iterators, so its `find` is renamed `find_value` too. Every old use fails to compile; none changes meaning silently (a probe of 30 idioms × 4 containers × clang/GCC × C++17/20 found none). To migrate:

  | before | after |
  |---|---|
  | `if (auto* v = m.find(k)) use(*v);` | `if (auto* v = m.find_value(k)) use(*v);` |
  | `m.find(k) != nullptr`, `s.find(k) != nullptr` | `m.contains(k)`, `s.contains(k)` |
  | `const auto* p = s.find(k); use(*p);` (a set) | `auto it = s.find(k); if (it != s.end()) use(*it);` |
  | `m.find_iterator(k)` | `m.find(k)` |
  | `table.find(k)` (`lookup_table`) | `table.find_value(k)` |

  On Cortex-M, `find_value` and `contains` cost the same as the old pointer `find` or less (local ARM GCC 16.2 and clang 23, `-Os`). `find(k) != end()` costs 4 to 14 bytes more per call site, because it loads the size to form `end()`.
- **Breaking (source):** the iterator of `flat_map` and `flat_set` is a class, not a pointer. It is the same size and has the same operations, and it is a contiguous iterator in C++20. It no longer converts to or from a pointer: use `&*it` or `it.operator->()` for the address, and `begin() + n` for a position. `erase` no longer checks that a position lies on an element boundary, because only a pointer cast could make one that does not. It still checks that the position is one of this container's elements. Dropping the check makes `erase_if` on the flat containers 20 to 24 bytes smaller on Cortex-M.
  An expression that changes a flat container and also reads its `end()`, such as `m.erase(it) == m.end()`, has no defined evaluation order. With a pointer iterator, GCC and clang happened to read `end()` after the erase. Now `==` is a function call, and GCC on x86-64 evaluates its arguments right to left: it reads the old `end()`, and the comparison is false. Read `end()` in a separate statement.
