"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_set_nth_off_by_one",
    "file": "include/metl/flat_set.hpp",
    "why": "positional access returns the NEXT element. This one survives "
           "all ctest targets except the replay -- the harness oracle is "
           "the only thing that looks at what nth() returns.",
    "kills": "ctest:replay_fuzz_flat_set",
    "old": """  METL_NODISCARD const_reference nth(size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return data()[index];
  }""",
    "new": """  METL_NODISCARD const_reference nth(size_type index) const noexcept {
    METL_ASSERT(index < size_);
    return data()[index + 1 < size_ ? index + 1 : index];
  }""",
}
