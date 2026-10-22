"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_generation_not_advanced",
    "file": "include/metl/handle_pool.hpp",
    "why": "release() saves the slot's current generation instead of the next "
           "one, so the slot's next handle repeats it and a stale handle to "
           "the old object resolves to the new one.",
    "kills": "ctest:handle_pool_test",
    "old": "    auto next = static_cast<generation_type>(generation_[index] + generation_type{1});",
    "new": "    auto next = static_cast<generation_type>(generation_[index]);",
}
