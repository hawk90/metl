"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::spsc_queue (#341)
MUTANT = {
    "name": "spsc_queue_pop_tail_load_relaxed",
    "file": "include/metl/spsc_queue.hpp",
    "why": "try_pop loads the producer's tail relaxed, so the element the "
           "producer constructed before its release store need not be visible "
           "when the consumer reads it.",
    "kills": "tsan:metl_spsc_queue_threaded_test",
    "old": """      cached_tail_ = tail_.load(std::memory_order_acquire);""",
    "new": """      cached_tail_ = tail_.load(std::memory_order_relaxed);""",
}
