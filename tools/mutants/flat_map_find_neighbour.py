"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_find_neighbour",
    "file": "include/metl/flat_map.hpp",
    "why": "find returns an iterator to the NEXT element. Keys stay sorted, size "
           "stays right, find and contains still agree on presence.",
    "kills": "ctest:flat_map|recoverable_api|associative_find",
    "old": """  METL_NODISCARD iterator find(const key_type& key) noexcept(compare_cannot_throw<key_type>) {
    const size_type index = lower_bound_index(key);
    if (index < comp_size_.value && !comp_size_.functor()(key, data()[index].key)) {
      return iterator_at(index);
    }""",
    "new": """  METL_NODISCARD iterator find(const key_type& key) noexcept(compare_cannot_throw<key_type>) {
    const size_type index = lower_bound_index(key);
    if (index < comp_size_.value && !comp_size_.functor()(key, data()[index].key)) {
      return iterator_at(index + 1 < comp_size_.value ? index + 1 : index);
    }""",
}
