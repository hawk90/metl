"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "mpmc_producer_retry_gives_up",
    "file": "include/metl/mpmc_queue.hpp",
    "why": "a producer that finds its ticket already taken by another producer reports the queue full instead of re-reading enqueue_pos_ and retrying, so a push fails on a queue with free slots (#622).",
    "kills": "ctest:mpmc_queue_retry",
    "old": """        // Another producer already advanced past us; re-read and retry.
        pos = enqueue_pos_.load(std::memory_order_relaxed);""",
    "new": """        // Another producer already advanced past us; re-read and retry.
        return false;""",
}
