"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "bitset_find_next_set_stays_in_word",
    "file": "include/metl/detail/bitset.hpp",
    "why": "find_next_set stops at the end of the word `from` is in, so a set bit in a later word is reported as none (N).",
    "kills": "ctest:metl_bitset_test",
    "old": '      if (++k == word_count) {\n        return N;\n      }\n      word = words_[k];',
    "new": '      if (++k <= word_count) {\n        return N;\n      }\n      word = words_[k];',
}
