- With libc++, a zero-length table in `fsm` or `lookup_table` no longer takes the room of one element (#696). `fsm`'s entry and exit hook tables default to zero entries, and libc++'s `std::array<T, 0>` reserves space for one `T`. So with libc++, an `fsm` without hooks carried two unused hooks. Host, clang + libc++:
  - `fsm<st, ev, 1>` goes from 80 to 40 B;
  - `fsm<st, ev, 0>` goes from 80 to 6 B;
  - `lookup_table<u32, u32, 0>` goes from 8 to 1 B.

  These are now the sizes libstdc++ already gave, so GCC builds and CI's ARM GCC layout probe are unchanged. Tables with entries are still `std::array`, with the same layout and code, and the constructors still take `std::array`. **Breaking (ABI, libc++ only):** the layout of these types changes there.
