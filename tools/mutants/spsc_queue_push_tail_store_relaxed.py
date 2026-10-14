"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::spsc_queue (#341)
MUTANT = {
    "name": "spsc_queue_push_tail_store_relaxed",
    "file": "include/metl/spsc_queue.hpp",
    "why": "try_emplace publishes the new tail relaxed, so a consumer that sees "
           "the tail can read the slot before the element in it is constructed.",
    "kills": "tsan:metl_spsc_queue_threaded_test",
    "old": """    tail_.store(next, std::memory_order_release);""",
    "new": """    tail_.store(next, std::memory_order_relaxed);""",
}
