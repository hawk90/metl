"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# the rest of the METL_HARDEN floor (#241)
MUTANT = {
    "name": "parse_int_harden_removed",
    "file": "include/metl/parse.hpp",
    "why": "the asserting parse_int returns a result built from the error.",
    "kills": "ctest:harden_floor_memory",
    "old": """  const expected<parsed<T>, parse_error> result = try_parse_int<T>(text);
  METL_HARDEN(result.has_value());""",
    "new": "  const expected<parsed<T>, parse_error> result = try_parse_int<T>(text);",
}
