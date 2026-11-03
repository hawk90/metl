"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "mpmc_consumer_retry_gives_up",
    "file": "include/metl/mpmc_queue.hpp",
    "why": "a consumer that finds its ticket already taken by another consumer reports the queue empty instead of re-reading dequeue_pos_ and retrying, so a pop fails on a queue that holds elements (#622).",
    "kills": "ctest:mpmc_queue_retry",
    "old": """      } else {
        pos = dequeue_pos_.load(std::memory_order_relaxed);""",
    "new": """      } else {
        return false;""",
}
