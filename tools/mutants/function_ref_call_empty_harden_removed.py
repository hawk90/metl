"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# the rest of the METL_HARDEN floor (#241)
MUTANT = {
    "name": "function_ref_call_empty_harden_removed",
    "file": "include/metl/function_ref.hpp",
    "why": "calling an empty function_ref jumps to address 0.",
    "kills": "ctest:harden_floor_memory",
    "old": "    METL_HARDEN(callback_ != nullptr);",
    "new": "",
}
