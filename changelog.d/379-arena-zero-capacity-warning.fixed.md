- **Allocating from `arena_allocator<0>` builds under GCC `-Wall -Werror`
  (#379).** GCC at `-O1` and above reported the record store into the
  zero-byte arena's placeholder as out of bounds. A zero-byte arena now returns
  null before touching that storage.
