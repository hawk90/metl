- **Emplacing an object larger than a static `arena_allocator` builds under GCC
  `-O2 -Wall -Werror` (#380).** GCC reported three `-Warray-bounds` errors for
  an arena in static storage, the usual embedded placement. `try_emplace` now
  returns null at compile time for an object that can never fit.
