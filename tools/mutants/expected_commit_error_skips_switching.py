"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::expected (#374)
MUTANT = {
    "name": "expected_commit_error_skips_switching",
    "file": "include/metl/expected.hpp",
    "why": "switching from value to error destroys the value while the expected "
           "still reads as holding it, so its destructor sees has_value() true.",
    "kills": "ctest:expected_reentrancy",
    "old": """  void commit_error(E&& incoming) {
    if constexpr (std::is_nothrow_move_constructible_v<E>) {
      state_ = detail::expected_state::switching;
      value_ptr()->~T();""",
    "new": """  void commit_error(E&& incoming) {
    if constexpr (std::is_nothrow_move_constructible_v<E>) {
      value_ptr()->~T();""",
}
