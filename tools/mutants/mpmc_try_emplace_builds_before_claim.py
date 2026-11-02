"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::mpmc_queue without exceptions (#575); the host build has them on.
MUTANT = {
    "name": "mpmc_try_emplace_builds_before_claim",
    "file": "include/metl/mpmc_queue.hpp",
    "why": "without exceptions, try_emplace builds the element before claiming a "
           "slot, so a full queue has already moved from the arguments (#575).",
    "kills": "qemu:cortex-m3:sync/mpmc_queue_test.cpp",
    "old": "std::is_nothrow_constructible_v<T, Args&&...> || METL_NO_EXCEPTIONS) {",
    "new": "std::is_nothrow_constructible_v<T, Args&&...>) {",
}
