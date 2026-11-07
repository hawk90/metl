"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::expected (#202)
MUTANT = {
    "name": "expected_void_copy_assign_self_check_inverted",
    "file": "include/metl/expected.hpp",
    "why": "copy assignment becomes a no-op for every other object, in "
           "expected<void, E> and expected<T, E> alike (one layer serves both): "
           "the state and the error are never copied.",
    "kills": "ctest:expected_test",
    "old": """    if (this == &other) {
      return *this;
    }
    this->copy_assign(""",
    "new": """    if (this != &other) {
      return *this;
    }
    this->copy_assign(""",
}
