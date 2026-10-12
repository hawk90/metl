"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

MUTANT = {
    "name": "hash_const_char_array_takes_pointer_overload",
    "file": "include/metl/hash.hpp",
    "why": "A non-template const char* overload wins over the template for a "
           "const char[N], so an unterminated const buffer is read past its end.",
    "kills": "ctest:hash_by_value_test",
    "old": """      return fnv1a(reinterpret_cast<const unsigned char*>(detail::addressof(value)), sizeof(T));
    }
  }
};""",
    "new": """      return fnv1a(reinterpret_cast<const unsigned char*>(detail::addressof(value)), sizeof(T));
    }
  }

  METL_NODISCARD std::size_t operator()(const char* str) const noexcept {
    std::size_t len = 0;
    while (str[len] != '\\0') {
      ++len;
    }
    return fnv1a(str, len);
  }
};""",
}
