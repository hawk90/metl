- A zero-capacity container or allocator no longer keeps storage for one element (#696). A zero-length array is ill-formed, so these types used to allocate `Capacity == 0 ? 1 : Capacity` slots, and padding grew that further. Host, 64-bit, the same with clang + libc++ and GCC 16 + libstdc++:
  - `fixed_vector<int, 0>` goes from 16 to 8 B;
  - `fixed_stack<int, 0>` goes from 16 to 8 B;
  - `ring_buffer`, `fixed_deque` and `fixed_queue` `<int, 0>` go from 24 to 16 B;
  - `object_pool<int, 0>` goes from 24 to 1 B (an empty class);
  - `handle_pool<int, 0>` goes from 32 to 1 B;
  - `event_dispatcher<void(int), 0>` goes from 32 to 1 B;
  - `arena_allocator<0>` and `monotonic_buffer<0>` go from 16 (clang) or 32 (GCC) to 1 B.

  How each type gets there:
  - `fixed_vector` and the ring family now hold their storage as an empty-at-zero base.
  - The others have a `Capacity == 0` specialisation that behaves as the general type does when it is full and empty at once. Every allocation fails, and the same preconditions assert, including the always-on alignment check.

  Every `Capacity >= 1` layout and its code are unchanged: the layout probe gives the same result on main and on this change (local clang, thumbv6m and thumbv7em). **Breaking (ABI):** the layout of the zero-capacity instantiations changes.
