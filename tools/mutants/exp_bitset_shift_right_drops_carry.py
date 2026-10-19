"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_bitset_shift_right_drops_carry",
    "file": "include/metl/detail/bitset.hpp",
    "why": "a shift right that does not carry the low bits of one word into "
           "the word below: the mirror of the shift-left mutant.",
    "kills": "ctest:metl_bitset_test",
    "old": "        if (bits != 0 && k + words + 1 < word_count) {",
    "new": "        if (bits == word_bits && k + words + 1 < word_count) {",
}
