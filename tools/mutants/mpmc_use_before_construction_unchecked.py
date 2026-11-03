"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::mpmc_queue (#290)
MUTANT = {
    "name": "mpmc_use_before_construction_unchecked",
    "file": "include/metl/mpmc_queue.hpp",
    "why": "a push or pop on a queue whose constructor has not run is not caught "
           "in a debug build: the constructor later wipes what was pushed.",
    "kills": "ctest:mpmc_queue_test",
    "old": """      METL_DASSERT(cells_[1].sequence.load(std::memory_order_relaxed) != 0);
""",
    "new": "",
}
