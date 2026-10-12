"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::variant (#202)
MUTANT = {
    "name": "scope_exit_throwing_store_skips_cleanup",
    "file": "include/metl/scope_exit.hpp",
    "why": "when storing the callable throws, the cleanup it was meant to "
           "guarantee never runs.",
    "kills": "ctest:conditional_noexcept",
    "old": """  catch (...) {
    func();
  }""",
    "new": """  catch (...) {
  }""",
}
