"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::fixed_any_invocable (#365)
MUTANT = {
    "name": "any_invocable_assign_from_empty_keeps_target",
    "file": "include/metl/fixed_function.hpp",
    "why": "move-assigning an empty fixed_any_invocable keeps the old target "
           "instead of destroying it.",
    "kills": "ctest:fixed_function_state_pairs",
    "old": """  fixed_any_invocable_impl& operator=(fixed_any_invocable_impl&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (!other.has_value()) {
      reset();
      return *this;
    }""",
    "new": """  fixed_any_invocable_impl& operator=(fixed_any_invocable_impl&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (!other.has_value()) {
      return *this;
    }""",
}
