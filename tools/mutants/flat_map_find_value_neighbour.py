"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_find_value_neighbour",
    "file": "include/metl/flat_map.hpp",
    "why": "find_value returns the NEXT element's value, while find stays right. "
           "Presence still answers correctly; only the value is wrong.",
    "kills": "ctest:flat_map|associative_find",
    "old": """  METL_NODISCARD mapped_type* find_value(const key_type& key) noexcept(compare_cannot_throw<key_type>) {
    const size_type index = lower_bound_index(key);
    if (index < comp_size_.value && !comp_size_.functor()(key, data()[index].key)) {
      return detail::addressof(data()[index].value);
    }""",
    "new": """  METL_NODISCARD mapped_type* find_value(const key_type& key) noexcept(compare_cannot_throw<key_type>) {
    const size_type index = lower_bound_index(key);
    if (index < comp_size_.value && !comp_size_.functor()(key, data()[index].key)) {
      return detail::addressof(data()[index + 1 < comp_size_.value ? index + 1 : index].value);
    }""",
}
