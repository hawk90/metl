"""A mutant for tools/check_mutants.py: one behaviour change a named gate must reject."""

# metl::detail::hash_key, used by static_unordered_map/set (#643)
MUTANT = {
    "name": "hash_key_drops_high_word",
    "file": "include/metl/hash.hpp",
    "why": "a key wider than size_t hashed by std::hash loses its high word again "
           "(libstdc++ casts), so keys that differ only there collide and lookups "
           "scan the table.",
    "kills": "tool:clang++ -std=gnu++17 -fsyntax-only -Iinclude -Itests tests/containers/unordered_wide_key_test.cpp",
    "old": """  if constexpr (std::is_same_v<Hash, std::hash<K>> && std::is_integral_v<K> &&
                (sizeof(K) > sizeof(std::size_t))) {""",
    "new": """  if constexpr (false) {""",
}
