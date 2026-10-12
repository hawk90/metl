"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::variant (#202)
MUTANT = {
    "name": "variant_visit_const_rvalue_same_index",
    "file": "include/metl/variant.hpp",
    "why": "the same on the const-rvalue overload.",
    "kills": "ctest:variant_test",
    "old": """    return visit_impl<Result, Index + 1>(std::forward<Visitor>(visitor),
                                         static_cast<const variant<Ts...>&&>(value));""",
    "new": """    return visit_impl<Result, Index>(std::forward<Visitor>(visitor),
                                     static_cast<const variant<Ts...>&&>(value));""",
}
