"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_assign_reports_inserted",
    "file": "include/metl/flat_map.hpp",
    "why": "try_insert_or_assign on a present key answers .second == true, std's 'inserted', although it only assigned.",
    "kills": "ctest:associative_insert_result",
    "old": "        data()[index].value = std::forward<V>(value);\n        return {iterator_at(index), false};",
    "new": "        data()[index].value = std::forward<V>(value);\n        return {iterator_at(index), true};",
}
