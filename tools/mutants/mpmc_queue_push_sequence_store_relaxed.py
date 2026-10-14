"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::mpmc_queue (#341)
MUTANT = {
    "name": "mpmc_queue_push_sequence_store_relaxed",
    "file": "include/metl/mpmc_queue.hpp",
    "why": "a producer hands its slot to the consumer with a relaxed sequence "
           "store, so the element it constructed need not be visible when the "
           "consumer moves it out.",
    "kills": "tsan:metl_mpmc_queue_threaded_test",
    "old": """    target->sequence.store(pos + 1, std::memory_order_release);""",
    "new": """    target->sequence.store(pos + 1, std::memory_order_relaxed);""",
}
