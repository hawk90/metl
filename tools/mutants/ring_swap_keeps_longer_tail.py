"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::ring_buffer / metl::fixed_deque swap (#348)
MUTANT = {
    "name": "ring_swap_keeps_longer_tail",
    "file": "include/metl/detail/ring_core.hpp",
    "why": "swap moves the longer ring's tail across but destroys all of it except "
           "the last element, so the longer ring keeps one element too many.",
    "kills": "ctest:container_swap",
    "old": """      while (longer.size_ > common) {
        --longer.size_;  // shrink first, then destroy, as in pop_front""",
    "new": """      while (longer.size_ > common + 1) {
        --longer.size_;  // shrink first, then destroy, as in pop_front""",
}
