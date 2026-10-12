"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# the rest of the METL_HARDEN floor (#241)
MUTANT = {
    "name": "fixed_any_invocable_noexcept_call_empty_harden_removed",
    "file": "include/metl/fixed_function.hpp",
    "why": "calling an empty noexcept fixed_any_invocable jumps through a "
           "null ops table.",
    "kills": "ctest:harden_floor_memory",
    "old": """  fixed_any_invocable& operator=(F&& function) {
    assign(std::forward<F>(function));
    return *this;
  }

  /// @brief Invokes the stored callable.
  /// @pre A target must be stored (has_value() is true); else asserts.
  R operator()(Args... args) const noexcept {
    METL_HARDEN(this->ops_ != nullptr);""",
    "new": """  fixed_any_invocable& operator=(F&& function) {
    assign(std::forward<F>(function));
    return *this;
  }

  /// @brief Invokes the stored callable.
  /// @pre A target must be stored (has_value() is true); else asserts.
  R operator()(Args... args) const noexcept {""",
}
