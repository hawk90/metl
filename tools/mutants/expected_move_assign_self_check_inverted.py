"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::expected (#202)
MUTANT = {
    "name": "expected_move_assign_self_check_inverted",
    "file": "include/metl/expected.hpp",
    "why": "move assignment returns early for every OTHER object, so it is "
           "a no-op. Live-object counts stay balanced; only the value is "
           "wrong, and only the fuzz harness's value oracle reads it.",
    "kills": "ctest:replay_fuzz_vocab",
    "old": """    if (this == &other) {
      return *this;
    }
    this->move_assign(""",
    "new": """    if (this != &other) {
      return *this;
    }
    this->move_assign(""",
}
