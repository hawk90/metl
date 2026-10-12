"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::variant (#202)
MUTANT = {
    "name": "variant_visit_rvalue_same_index",
    "file": "include/metl/variant.hpp",
    "why": "rvalue visit recurses on the SAME index, so any alternative "
           "but the first never terminates. Every test visited index 0.",
    "kills": "ctest:variant_test",
    "old": """    return visit_impl<Result, Index + 1>(std::forward<Visitor>(visitor),
                                         static_cast<variant<Ts...>&&>(value));""",
    "new": """    return visit_impl<Result, Index>(std::forward<Visitor>(visitor),
                                     static_cast<variant<Ts...>&&>(value));""",
}
