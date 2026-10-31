"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "assert_strings_off_keeps_expression",
    "file": "include/metl/config.hpp",
    "why": "METL_ASSERT_STRINGS=0 still stores every checked expression, so the "
           "flash it promises to save stays spent.",
    "kills": "ctest:metl_assert_strings_off_test",
    "old": """#define METL_DETAIL_CHECK_EXPRESSION(expr) ""
""",
    "new": """#define METL_DETAIL_CHECK_EXPRESSION(expr) #expr
""",
}
