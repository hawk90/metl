"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::atomic_ref (#367)
MUTANT = {
    "name": "atomic_ref_cas_failure_order_releases",
    "file": "include/metl/atomic_ref.hpp",
    "why": "a compare-exchange with acq_rel keeps acq_rel as its failure order, "
           "which the builtin does not accept: the failure order may not release.",
    "kills": "ctest:atomic_ref",
    "old": """    case std::memory_order_acq_rel:
      return __ATOMIC_ACQUIRE;""",
    "new": """    case std::memory_order_acq_rel:
      return __ATOMIC_ACQ_REL;""",
}
