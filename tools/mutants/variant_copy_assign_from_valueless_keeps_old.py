"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::variant (#202)
MUTANT = {
    "name": "variant_copy_assign_from_valueless_keeps_old",
    "file": "include/metl/variant.hpp",
    "why": "copy-assigning a valueless variant leaves the target holding "
           "its old alternative instead of becoming valueless.",
    "kills": "ctest:variant_regression",
    "old": """      reset();
      return *this;
    }
    assign_from(other);""",
    "new": """      return *this;
    }
    assign_from(other);""",
}
