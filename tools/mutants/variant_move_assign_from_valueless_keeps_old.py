"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::variant (#202)
MUTANT = {
    "name": "variant_move_assign_from_valueless_keeps_old",
    "file": "include/metl/variant.hpp",
    "why": "the same for move assignment.",
    "kills": "ctest:variant_regression",
    "old": """      reset();
      return *this;
    }
    assign_from(static_cast<variant&&>(other));""",
    "new": """      return *this;
    }
    assign_from(static_cast<variant&&>(other));""",
}
