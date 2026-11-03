"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::expected (#474)
MUTANT = {
    "name": "expected_never_trivially_destructible",
    "file": "include/metl/expected.hpp",
    "why": "expected keeps its own destructor for trivially destructible T and E: "
           "every caller pays for a destructor check that cannot fire, and it is "
           "not a literal type in C++17.",
    "kills": "tool:clang++ -std=c++17 -fsyntax-only -Iinclude -Itests tests/vocab/expected_trivial_destructor_test.cpp",
    "old": """template <typename T, typename E, bool Trivial = expected_trivially_destructible_v<T, E>>
struct expected_members {""",
    "new": """template <typename T, typename E, bool Trivial = false>
struct expected_members {""",
}
