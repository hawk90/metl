"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "exp_isched_mid_round_attach_polled",
    "file": "tests/exp/intrusive_scheduler.hpp",
    "why": "a task attached during a round is linked before the round's end marker, so the round polls it, against the contract the array scheduler keeps.",
    "kills": "ctest:metl_intrusive_scheduler_test",
    "old": "    tasks_.push_back(node);",
    "new": "    if (rounds_ > 0) {\n      auto e = tasks_.end();\n      --e;\n      (void)tasks_.insert(e, node);\n    } else {\n      tasks_.push_back(node);\n    }",
}
