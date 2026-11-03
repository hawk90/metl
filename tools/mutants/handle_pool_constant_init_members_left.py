"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_constant_init_members_left",
    "file": "include/metl/handle_pool.hpp",
    "why": "the constant-initializing constructor leaves the slots and generations "
           "uninitialized, so a constinit pool no longer compiles and the pool an "
           "early interrupt reaches is built at run time again (#408).",
    # The C++20 build of handle_pool_constant_init_test declares constinit pools.
    "kills": "tool:clang++ -std=c++20 -fsyntax-only -Iinclude -Itests tests/memory/handle_pool_constant_init_test.cpp",
    "old": """      : storage_{}, generation_{}, free_head_(0), size_(0) {""",
    "new": """      : free_head_(0), size_(0) {""",
}
