"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "handle_pool_const_get_past_end",
    "file": "include/metl/handle_pool.hpp",
    "why": "the const get() turns the 'does not resolve' index into "
           "slot_ptr(Capacity), so a stale or null handle returns a "
           "pointer one past the slots.",
    "kills": "ctest:handle_pool_test",
    "old": """  METL_NODISCARD const_pointer get(handle_type handle) const noexcept {
    const size_type index = live_index(handle);
    return index < Capacity ? slot_ptr(index) : nullptr;""",
    "new": """  METL_NODISCARD const_pointer get(handle_type handle) const noexcept {
    const size_type index = live_index(handle);
    return index <= Capacity ? slot_ptr(index) : nullptr;""",
}
