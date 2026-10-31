"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "bitset_find_next_set_ignores_from",
    "file": "include/metl/detail/bitset.hpp",
    "why": "find_next_set(from) keeps the set bits below `from` in its word, so it returns an index before `from` and an iteration over the set bits never ends.",
    "kills": "ctest:metl_bitset_test",
    "old": '    auto word = static_cast<word_type>(words_[k] & (~word_type{0} << (from % word_bits)));',
    "new": '    auto word = static_cast<word_type>(words_[k]);',
}
