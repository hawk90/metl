"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_generation_of_raw",
    "file": "include/metl/handle_pool.hpp",
    "why": "generation_of() reports a free slot's stored even generation, 0 "
           "for a slot never used, instead of the one its next handle carries.",
    "kills": "ctest:handle_pool_test",
    "old": "    return static_cast<generation_type>(generation_[index] | generation_type{1});",
    "new": "    return generation_[index];",
}
