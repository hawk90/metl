"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "ring_buffer_push_back_move_ignores_full",
    "file": "include/metl/detail/ring_core.hpp",
    "why": "move push_back on a full ring drops the value and returns the old back, so the caller carries on with the wrong element.",
    "kills": "ctest:metl_container_assert_trips_test",
    "old": "reference push_back(T&& value) { return emplace_back(std::move(value)); }",
    "new": "reference push_back(T&& value) { (void)try_emplace_back(std::move(value)); return this->back(); }",
}
