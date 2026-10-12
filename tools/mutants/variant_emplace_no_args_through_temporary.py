"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "variant_emplace_no_args_through_temporary",
    "file": "include/metl/variant.hpp",
    "why": "emplace with no arguments switches through a temporary, paying a "
           "whole alternative of stack and an extra move where nothing can alias "
           "the storage (#315).",
    "kills": "ctest:variant_test",
    "old": """      reset();
      ::new (raw_addr()) target_type();
      index_ = I;
    } else {""",
    "new": """      emplace_through_temporary<I>();
    } else {""",
}
