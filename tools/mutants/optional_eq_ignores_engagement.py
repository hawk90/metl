"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::optional (#365)
MUTANT = {
    "name": "optional_eq_ignores_engagement",
    "file": "include/metl/optional.hpp",
    "why": "optional == optional skips the engagement check: an empty optional "
           "compares equal to an engaged one.",
    "kills": "ctest:optional_state_pairs",
    "old": """constexpr auto operator==(const optional<T>& lhs, const optional<U>& rhs) -> decltype(*lhs == *rhs, bool()) {
  if (lhs.has_value() != rhs.has_value())
    return false;
""",
    "new": """constexpr auto operator==(const optional<T>& lhs, const optional<U>& rhs) -> decltype(*lhs == *rhs, bool()) {
""",
}
