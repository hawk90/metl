"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "scheduler_until_idle_stops_on_quiet_round",
    "file": "include/metl/coro/scheduler.hpp",
    "why": "run_until_idle stops after a round in which nothing yielded, "
           "returning with a task attached during that round unpolled.",
    "kills": "ctest:coro_scheduler_test",
    "old": """    (void)run_once();
    ++rounds;
  }""",
    "new": """    const size_type running = run_once();
    ++rounds;
    if (running == 0) {
      break;
    }
  }""",
}
