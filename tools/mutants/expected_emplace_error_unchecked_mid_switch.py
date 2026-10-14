"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::expected (#374)
MUTANT = {
    "name": "expected_emplace_error_unchecked_mid_switch",
    "file": "include/metl/expected.hpp",
    "why": "emplace_error no longer refuses a call from a member's destructor, "
           "which then builds over a member that is being destroyed.",
    "kills": "ctest:expected_reentrancy",
    "old": """  E& emplace_error(Args&&... args) {
    require_settled();
    if (holds_value()) {
      reinit_as_error(std::forward<Args>(args)...);""",
    "new": """  E& emplace_error(Args&&... args) {
    if (holds_value()) {
      reinit_as_error(std::forward<Args>(args)...);""",
}
