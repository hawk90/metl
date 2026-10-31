"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "event_dispatcher_try_subscribe_accepts_empty",
    "file": "include/metl/event_dispatcher.hpp",
    "why": "try_subscribe stores an empty delegate, which takes a slot and returns an id for a listener that does not exist.",
    "kills": "ctest:metl_r1_forms_test",
    "old": '    if (!listener) {\n      return nullopt;\n    }\n',
    "new": '',
}
