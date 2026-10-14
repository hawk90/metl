"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::optional (#340)
MUTANT = {
    "name": "optional_engaged_before_construction",
    "file": "include/metl/optional.hpp",
    "why": "the optional marks itself engaged before T's constructor runs, so a "
           "throwing constructor leaves it engaged over no object.",
    "kills": "ctest:conformance_throwing",
    "old": """    detail::construct_at(detail::addressof(storage_.value_), std::forward<Args>(args)...);
    has_value_ = true;""",
    "new": """    has_value_ = true;
    detail::construct_at(detail::addressof(storage_.value_), std::forward<Args>(args)...);""",
}
