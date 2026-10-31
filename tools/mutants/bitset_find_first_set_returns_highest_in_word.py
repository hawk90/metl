"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "bitset_find_first_set_returns_highest_in_word",
    "file": "include/metl/detail/bitset.hpp",
    "why": "find_first_set returns the highest set bit of the first non-empty word instead of the lowest, so a word holding two set bits reports the wrong one.",
    "kills": "ctest:metl_bitset_test",
    "old": '        return (k * word_bits) + static_cast<size_type>(metl::countr_zero(words_[k]));',
    "new": '        return (k * word_bits) + (word_bits - 1 - static_cast<size_type>(metl::countl_zero(words_[k])));',
}
