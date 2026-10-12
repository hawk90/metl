"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::variant (#202)
MUTANT = {
    "name": "variant_valueless_not_equal_flipped",
    "file": "include/metl/variant.hpp",
    "why": "two valueless variants compare not-equal.",
    "kills": "ctest:variant_regression",
    "old": """  if (lhs.valueless_by_exception()) {
    return false;
  }
  return detail::compare_alternative<detail::cmp_ne, 0>(lhs, rhs);""",
    "new": """  if (lhs.valueless_by_exception()) {
    return true;
  }
  return detail::compare_alternative<detail::cmp_ne, 0>(lhs, rhs);""",
}
