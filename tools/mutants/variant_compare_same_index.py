"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::variant (#202)
MUTANT = {
    "name": "variant_compare_same_index",
    "file": "include/metl/variant.hpp",
    "why": "comparison recurses on the SAME index, so comparing two "
           "variants that both hold alternative 1 or later never returns.",
    "kills": "ctest:variant_test",
    "old": "    return compare_alternative<Op, I + 1>(lhs, rhs);",
    "new": "    return compare_alternative<Op, I>(lhs, rhs);",
}
