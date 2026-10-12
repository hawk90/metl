"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "visit_single_result_guard_removed",
    "file": "include/metl/variant.hpp",
    "why": "#84's guard becomes vacuous, so metl::visit goes back to "
           "silently truncating a visitor's result.",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": """template <typename Visitor, typename... Ts>
inline constexpr bool visit_single_result_lvalue_v =
    (std::is_same_v<visit_result_lvalue_t<Visitor, Ts...>,
                    decltype(std::declval<Visitor>()(std::declval<Ts&>()))> &&
     ...);""",
    "new": """template <typename Visitor, typename... Ts>
inline constexpr bool visit_single_result_lvalue_v = true;""",
}
