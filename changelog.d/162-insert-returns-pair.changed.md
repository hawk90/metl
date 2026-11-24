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

  On Cortex-M (local ARM GCC 16.2 and clang 23, `-Os`, M0 to M7), a call site that reads only `.second` costs 4 bytes more: the flag is taken from the word the insert returns. A call site that goes on to use the element saves the second lookup: 6 bytes on `flat_map` and 44 to 46 on `static_unordered_map` with GCC, and up to 4 with clang, which spends 4 to 6 more on `flat_map`. A translation unit holding those call sites and the insert code they use is 8 to 10 bytes larger on `flat_map` with GCC, and 56 to 104 bytes smaller in the other three cases.
- **Changed:** `emplace` on `flat_map` and `flat_set` with a key that is already present returns the element there, as the unordered containers already did. It used to trip an assertion. The unordered containers' `emplace` now finds the key and its slot in one probe sequence instead of two.
