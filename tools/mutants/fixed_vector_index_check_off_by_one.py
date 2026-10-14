"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::fixed_vector (#364)
MUTANT = {
    "name": "fixed_vector_index_check_off_by_one",
    "file": "include/metl/fixed_vector.hpp",
    "why": "operator[]'s precondition admits index == size(), one past the last "
           "element, so a read there goes unchecked.",
    "kills": "ctest:container_assert_trips",
    "old": """  METL_NODISCARD reference operator[](size_type index) noexcept {
    METL_ASSERT(index < size_);""",
    "new": """  METL_NODISCARD reference operator[](size_type index) noexcept {
    METL_ASSERT(index <= size_);""",
}
