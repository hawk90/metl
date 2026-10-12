"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::variant (#202)
MUTANT = {
    "name": "flat_set_clear_relocates",
    "file": "include/metl/flat_set.hpp",
    "why": "clear() moves each element into a temporary before destroying it: "
           "N needless moves, and std::terminate when the move throws.",
    "kills": "ctest:throwing_element",
    "old": """    while (size_ > 0) {
      --size_;
      data()[size_].~value_type();
    }""",
    "new": """    while (size_ > 0) {
      erase_at(size_ - 1);
    }""",
}
