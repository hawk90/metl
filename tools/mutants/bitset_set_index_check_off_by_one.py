"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "bitset_set_index_check_off_by_one",
    "file": "include/metl/detail/bitset.hpp",
    "why": "set(N) passes the index check, so it writes bit N: a tail bit in "
           "the last word, or past the storage when N is a multiple of 32.",
    "kills": "ctest:metl_bitset_test",
    "old": "  constexpr void set(size_type index) noexcept {\n    METL_HARDEN(index < N);",
    "new": "  constexpr void set(size_type index) noexcept {\n    METL_HARDEN(index <= N);",
}
