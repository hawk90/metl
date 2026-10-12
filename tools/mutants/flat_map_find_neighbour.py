"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_find_neighbour",
    "file": "include/metl/flat_map.hpp",
    "why": "find returns the NEXT element's value. Keys stay sorted, size "
           "stays right, find and contains still agree with each other.",
    "kills": "ctest:flat_map|recoverable_api",
    "old": """  METL_NODISCARD mapped_type* find(const key_type& key) noexcept(compare_cannot_throw<key_type>) {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index].key)) {
      return detail::addressof(data()[index].value);
    }""",
    "new": """  METL_NODISCARD mapped_type* find(const key_type& key) noexcept(compare_cannot_throw<key_type>) {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index].key)) {
      return detail::addressof(data()[index + 1 < size_ ? index + 1 : index].value);
    }""",
}
