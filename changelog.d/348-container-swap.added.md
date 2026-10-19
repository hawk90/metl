- Member and ADL `swap` for `fixed_string`, `fixed_deque`, `fixed_queue`,
  `fixed_stack`, `fixed_priority_queue`, `ring_buffer`, `flat_map`, `flat_set`,
  `static_unordered_map` and `static_unordered_set` (#348). They swap element by
  element, so swapping containers of n and m elements moves at most the larger
  of n and m elements (`n + m` for the hash containers), where `std::swap` moved
  every element three times. Each is `noexcept` when the element's move and swap
  are. If one throws, a sorted, heap or hash container is left empty, and the
  others keep valid elements. The free `swap` is declared only for swappable
  elements, so `using std::swap; swap(a, b)` still compiles for the rest.
