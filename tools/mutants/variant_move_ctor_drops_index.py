"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::variant (#365)
MUTANT = {
    "name": "variant_move_ctor_drops_index",
    "file": "include/metl/variant.hpp",
    "why": "the move constructor moves the alternative in but leaves the new "
           "variant valueless: a live object nothing will destroy.",
    "kills": "ctest:variant_state_pairs",
    "old": """      move_ops()[other.index_](raw_addr(), other.raw_addr());
      index_ = other.index_;
    }
  }

  /// @brief Destroys the active alternative.""",
    "new": """      move_ops()[other.index_](raw_addr(), other.raw_addr());
    }
  }

  /// @brief Destroys the active alternative.""",
}
