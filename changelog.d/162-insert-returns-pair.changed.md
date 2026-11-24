- **Breaking (source):** `try_emplace`, `emplace` and `try_insert_or_assign` on `flat_map`, `flat_set`, `static_unordered_map` and `static_unordered_set` return `std::pair<iterator, bool>`, as std's do (#162). The old `bool` could not tell "already present" from "full". Now:
  - a new key gives `{iterator to it, true}`;
  - a present key gives `{iterator to the element already there, false}`, and is found even when the container is full;
  - a new key with no room gives `{end(), false}` from the `try_` forms. `emplace` asserts instead, as before.

  `try_insert_or_assign`'s `bool` now means what std's does: "inserted rather than assigned". It used to mean "it fit", which was also true for an assignment; an assignment now gives `{iterator, false}`. `insert_or_assign` still returns a reference.

  Every old use as a `bool` or a reference fails to compile. Only a discarded result still compiles, such as `(void)m.try_emplace(k, v);` or `m.emplace(k, v);`, and it does what it did, except that `emplace` of a present key on a flat container no longer asserts (below). That is from a probe of 22 idioms × 2 containers × clang/GCC × C++17/20. To migrate:

  | before | after |
  |---|---|
  | `if (m.try_emplace(k, v))` | `if (m.try_emplace(k, v).second)` |
  | `if (!m.try_insert_or_assign(k, v))` (full) | `if (m.try_insert_or_assign(k, v).first == m.end())` |
  | `auto& e = m.emplace(k, v);` | `auto& e = *m.emplace(k, v).first;` |
  | `m.try_emplace(k, v); auto* p = m.find_value(k);` | `auto [it, inserted] = m.try_emplace(k, v);` |

  Measured on Cortex-M with ARM GCC 14.2, the compiler CI uses, at `-Os` on M0 to M7:
  - Where the insert is inlined into the caller, which is usual when a container is filled from one or two places, the pair costs nothing. Three inlined inserts in one function cost 0 to 4 bytes less than before. The size probe shrinks by 4 to 8 bytes on M3, M4 and M7. On M0 it grows by 24 bytes, because Thumb-1 has eight low registers and the iterator held across the loop spills to the stack.
  - Where the insert stays out of line, a call site that reads only `.second` costs 8 to 12 bytes more, because a pair comes back through memory.
  - A call site that goes on to use the element saves the second lookup. That is 10 to 14 bytes on `flat_map` and 34 to 36 on `static_unordered_map`.
  - A translation unit holding those call sites and the insert code they use is 20 to 28 bytes larger on `flat_map`, and between 6 bytes smaller and 4 bytes larger on `static_unordered_map`.
- **Changed:** `emplace` on `flat_map` and `flat_set` with a key that is already present returns the element there, as the unordered containers already did. It used to trip an assertion. The unordered containers' `emplace` now finds the key and its slot in one probe sequence instead of two.
