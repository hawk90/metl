"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_set_emplace_full_harden_removed",
    "file": "include/metl/flat_set.hpp",
    "why": "emplace of a new key into a full flat_set returns a one-past- "
           "the-end reference, or another key's element (#580), at METL_HARDENING_NONE.",
    "kills": "ctest:harden_floor_memory",
    "old": '      // Hard guard on the full-set path; see the twin on flat_map::emplace.\n      METL_HARDEN(inserted);',
    "new": '      // Hard guard on the full-set path; see the twin on flat_map::emplace.\n      (void)inserted;',
}
