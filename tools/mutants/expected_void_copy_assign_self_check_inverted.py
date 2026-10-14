"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::expected (#202)
MUTANT = {
    "name": "expected_void_copy_assign_self_check_inverted",
    "file": "include/metl/expected.hpp",
    "why": "expected<void, E> copy assignment becomes a no-op for every "
           "other object; the state and the error are never copied.",
    "kills": "ctest:expected_test",
    "old": """  expected& operator=(const expected& other) {
    require_settled();
    other.require_settled();
    if (this == &other) {
      return *this;
    }

    if (other.holds_value()) {
      if (!holds_value()) {""",
    "new": """  expected& operator=(const expected& other) {
    require_settled();
    other.require_settled();
    if (this != &other) {
      return *this;
    }

    if (other.holds_value()) {
      if (!holds_value()) {""",
}
