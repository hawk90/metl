"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::atomic_ref (#367)
MUTANT = {
    "name": "atomic_ref_release_maps_to_acquire",
    "file": "include/metl/atomic_ref.hpp",
    "why": "memory_order_release maps to the acquire builtin order: a release "
           "store or exchange on the word path loses its ordering.",
    "kills": "ctest:atomic_ref",
    "old": """    case std::memory_order_release:
      return __ATOMIC_RELEASE;""",
    "new": """    case std::memory_order_release:
      return __ATOMIC_ACQUIRE;""",
}
