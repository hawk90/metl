"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::fixed_deque (#638)
MUTANT = {
    "name": "deque_front_reentrant_throw_unchecked",
    "file": "include/metl/fixed_deque.hpp",
    "why": "a constructor that pushes to the front of its own deque and then "
           "throws is not refused: head_ is restored past the inner element, "
           "and the unconstructed slot becomes back().",
    "kills": "ctest:reentrant_throw",
    "old": """      METL_HARDEN(this->head_ == slot && this->size_ == claimed);
""",
    "new": "",
}
