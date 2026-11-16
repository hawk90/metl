"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "flat_map_linear_scan",
    "file": "include/metl/flat_map.hpp",
    "why": "binary search becomes a linear scan. Same results, same order, "
           "same sizes -- only the comparison count moves.",
    "kills": "ctest:operation_count",
    "old": """    size_type first = 0;
    size_type count = comp_size_.value;
    while (count > 0) {
      const size_type step = count / 2;
      const size_type index = first + step;
      if (comp_size_.functor()(data()[index].key, key)) {
        first = index + 1;
        count -= step + 1;
      } else {
        count = step;
      }
    }
    return first;
  }

  template <typename K>
  size_type upper_bound_index""",
    "new": """    size_type first = 0;
    while (first < comp_size_.value && comp_size_.functor()(data()[first].key, key)) {
      ++first;
    }
    return first;
  }

  template <typename K>
  size_type upper_bound_index""",
}
