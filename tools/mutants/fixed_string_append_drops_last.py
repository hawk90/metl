"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "fixed_string_append_drops_last",
    "file": "include/metl/fixed_string.hpp",
    "why": "try_append copies one character too few but reports the full "
           "length. The NUL terminator is still where size() says.",
    "kills": "ctest:fixed_string|format",
    "old": """    copy_in(size_, text, input_size);
""",
    "new": """    copy_in(size_, text, input_size == 0 ? 0 : input_size - 1);
""",
}
