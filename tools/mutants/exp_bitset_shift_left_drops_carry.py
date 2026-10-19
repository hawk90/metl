"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_bitset_shift_left_drops_carry",
    "file": "include/metl/detail/bitset.hpp",
    "why": "a shift left that does not carry the top bits of one word into "
           "the next: shifts within a word still work, so only a set wider "
           "than one word, shifted by a count that is not a multiple of 32, "
           "shows it.",
    "kills": "ctest:metl_bitset_test",
    "old": "        if (bits != 0 && k > words) {",
    "new": "        if (bits != 0 && k > words + word_count) {",
}
