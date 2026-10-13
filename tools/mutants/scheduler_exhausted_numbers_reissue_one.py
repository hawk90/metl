"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::coro::scheduler (#367)
MUTANT = {
    "name": "scheduler_exhausted_numbers_reissue_one",
    "file": "include/metl/coro/scheduler.hpp",
    "why": "when nested rounds hold every attachment number, attaching hands out "
           "one of them again instead of reporting false, as try_attach promises.",
    "kills": "ctest:coro_scheduler_reentrancy",
    "old": """      if (!in_use(candidate)) {
        out = candidate;
        return true;
      }
    }
    return false;""",
    "new": """      if (!in_use(candidate)) {
        out = candidate;
        return true;
      }
    }
    out = next_attachment_;
    return true;""",
}
