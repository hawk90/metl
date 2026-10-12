"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "fixed_vector_equal_ignores_size",
    "file": "include/metl/fixed_vector.hpp",
    "why": "operator== treats a vector as equal to any longer one it is a "
           "prefix of.",
    "kills": "ctest:fixed_vector_test",
    "old": """  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (!(lhs[i] == rhs[i])) {""",
    "new": """  if (lhs.size() > rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    if (!(lhs[i] == rhs[i])) {""",
}
