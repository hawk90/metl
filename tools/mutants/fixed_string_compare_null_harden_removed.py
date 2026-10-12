"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "fixed_string_compare_null_harden_removed",
    "file": "include/metl/fixed_string.hpp",
    "why": "comparing a fixed_string with a null const char* reads "
           "through the null pointer at METL_HARDENING_NONE.",
    "kills": "ctest:harden_floor_memory",
    "old": """  int compare_c(const char* text) const noexcept {
    METL_HARDEN(text != nullptr);""",
    "new": """  int compare_c(const char* text) const noexcept {""",
}
