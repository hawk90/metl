"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "fixed_vector_try_assign_n_rejects_exact",
    "file": "include/metl/fixed_vector.hpp",
    "why": "try_assign(n, value) refuses n == Capacity, which fits.",
    "kills": "ctest:fixed_vector_test",
    "old": """  METL_NODISCARD bool try_assign(size_type n, const T& value) {
    if (n > Capacity) {""",
    "new": """  METL_NODISCARD bool try_assign(size_type n, const T& value) {
    if (n >= Capacity) {""",
}
