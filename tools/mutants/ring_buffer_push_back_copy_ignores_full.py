"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "ring_buffer_push_back_copy_ignores_full",
    "file": "include/metl/detail/ring_core.hpp",
    "why": "copy push_back on a full ring drops the value and returns the old back, so the caller carries on with the wrong element.",
    "kills": "ctest:metl_container_assert_trips_test",
    "old": "reference push_back(const T& value) { return emplace_back(value); }",
    "new": "reference push_back(const T& value) { (void)try_emplace_back(value); return this->back(); }",
}
