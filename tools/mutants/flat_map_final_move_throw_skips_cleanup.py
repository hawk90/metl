"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::variant (#202)
MUTANT = {
    "name": "flat_map_final_move_throw_skips_cleanup",
    "file": "include/metl/flat_map.hpp",
    "why": "a throw moving the entry into the gap leaves a dead slot inside "
           "size_: the destructor destroys it and leaks the last element.",
    "kills": "ctest:throwing_element",
    "old": """      } catch (...) {
        for (size_type j = 0; j < index; ++j) {
          data()[j].~value_type();
        }
        for (size_type j = index + 1; j <= size_; ++j) {
          data()[j].~value_type();
        }
        size_ = 0;
        throw;
      }""",
    "new": """      } catch (...) {
        throw;
      }""",
}
