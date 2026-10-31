"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "bitset_find_first_clear_drops_word_offset",
    "file": "include/metl/detail/bitset.hpp",
    "why": "find_first_clear returns the bit's place within its word, so a first clear bit past the first word is reported at the wrong index.",
    "kills": "ctest:metl_bitset_test",
    "old": '        return (k * word_bits) + static_cast<size_type>(metl::countr_zero(clear));',
    "new": '        return static_cast<size_type>(metl::countr_zero(clear));',
}
