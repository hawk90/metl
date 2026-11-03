"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::detail::ring_core, behind ring_buffer and fixed_deque (#638)
MUTANT = {
    "name": "ring_reentrant_throw_unchecked",
    "file": "include/metl/detail/ring_core.hpp",
    "why": "a constructor that pushes into its own ring and then throws is not "
           "refused: the unconstructed slot stays inside the live range.",
    "kills": "ctest:reentrant_throw",
    "old": """      METL_HARDEN(size_ == claimed);
""",
    "new": "",
}
