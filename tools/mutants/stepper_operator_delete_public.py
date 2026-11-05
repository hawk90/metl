"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "stepper_operator_delete_public",
    "file": "include/metl/coro/stepper.hpp",
    "why": "stepper's class-scope operator delete becomes public, so `delete p` through "
           "a stepper* compiles again and frees nothing (#548).",
    "kills": "ctest:coro_stepper_test",
    "old": "  // NOLINTNEXTLINE(cert-dcl54-cpp,misc-new-delete-overloads)\n"
           "  static void operator delete(void*) noexcept {}\n",
    "new": "  // NOLINTNEXTLINE(cert-dcl54-cpp,misc-new-delete-overloads)\n"
           " public:\n  static void operator delete(void*) noexcept {}\n protected:\n",
}
