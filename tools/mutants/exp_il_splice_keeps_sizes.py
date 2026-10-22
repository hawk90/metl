"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_il_splice_keeps_sizes",
    "file": "include/metl/exp/intrusive_list.hpp",
    "why": "splicing into another list leaves both sizes as they were.",
    "kills": "ctest:metl_intrusive_list_test",
    "old": "      --other.size_;\n      ++size_;\n",
    "new": "",
}
