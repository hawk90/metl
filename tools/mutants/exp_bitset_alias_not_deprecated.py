"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_bitset_alias_not_deprecated",
    "file": "include/metl/exp/bitset.hpp",
    "why": "metl::exp::bitset stops warning, so code that still uses it learns of "
           "the 2.0 removal only when it no longer compiles (#645).",
    "kills": "tool:tools/check_compile_fail.py --cxx clang++",
    "old": 'using bitset METL_DEPRECATED(\n    "metl::exp::bitset is removed in 2.0; use metl::bitset from <metl/bitset.hpp> (#527)") = metl::bitset<N>;',
    "new": 'using bitset = metl::bitset<N>;',
}
