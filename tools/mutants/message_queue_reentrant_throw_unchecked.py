"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::static_message_queue (#638)
MUTANT = {
    "name": "message_queue_reentrant_throw_unchecked",
    "file": "include/metl/static_message_queue.hpp",
    "why": "a constructor that pushes into its own queue and then throws is not "
           "refused: tail_ is rewound under the inner element.",
    "kills": "ctest:reentrant_throw",
    "old": """      METL_HARDEN(size_ == claimed);
""",
    "new": "",
}
