- An empty comparator, hasher or key equality no longer takes room (#318). Each is held with a count in one member, as an empty base of that member, so it costs nothing. Before, it cost a byte plus padding. Host, 64-bit, the same with clang + libc++ and GCC 16 + libstdc++:
  - `flat_map<u8, u8, 8>` goes from 32 to 24 B, and `flat_map<u32, u32, 8>` from 80 to 72 B;
  - `flat_set<u8, 8>` goes from 24 to 16 B, and `flat_set<u32, 8>` from 48 to 40 B;
  - `fixed_priority_queue<u8, 8>` goes from 24 to 16 B, and `fixed_priority_queue<u32, 8>` from 48 to 40 B;
  - `static_unordered_map<u8, u8, 8>` goes from 64 to 56 B, and `static_unordered_set<u8, 8>` from 48 to 40 B;
  - `coro::deadline_scheduler<4>` goes from 160 to 152 B.

  A stateful or `final` functor is held as before. This changes the layout of each of these types, so it is an ABI break.
- `flat_set<T, 0>` goes from 16 to 8 B, and `flat_map<K, T, 0>` from 24 to 16 B: neither keeps storage for an element (#696). `flat_map` keeps an empty storage member, which costs a byte plus padding, because its nested `value_type` cannot be named in a base list.
