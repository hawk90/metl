#pragma once

#include "metl/compiler.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

namespace metl {

namespace detail {

// FNV-1a constants selected at compile time based on size_t width.
template <std::size_t Width>
struct fnv_constants;

template <>
struct fnv_constants<8> {
  static constexpr std::size_t offset = static_cast<std::size_t>(0xcbf29ce484222325ULL);
  static constexpr std::size_t prime = static_cast<std::size_t>(0x100000001b3ULL);
};

template <>
struct fnv_constants<4> {
  static constexpr std::size_t offset = static_cast<std::size_t>(0x811c9dc5UL);
  static constexpr std::size_t prime = static_cast<std::size_t>(0x01000193UL);
};

using active_fnv = fnv_constants<sizeof(std::size_t)>;

}  // namespace detail

// FNV-1a hash over a contiguous unsigned char buffer. constexpr-friendly.
METL_NODISCARD constexpr std::size_t fnv1a(const unsigned char* data, std::size_t len) noexcept {
  std::size_t hash = detail::active_fnv::offset;
  for (std::size_t i = 0; i < len; ++i) {
    hash ^= static_cast<std::size_t>(data[i]);
    hash *= detail::active_fnv::prime;
  }
  return hash;
}

// FNV-1a hash over a buffer of bytes. The template parameter T is only used
// to allow callers to pass typed pointers; the bytes are consumed byte-by-byte
// regardless of T. Not constexpr-evaluable for non-byte T (requires
// reinterpret_cast), but `constexpr` qualified so it composes in constant
// contexts when T happens to be a byte type.
template <typename T>
METL_NODISCARD constexpr std::size_t fnv1a(const T* data, std::size_t len) noexcept {
  std::size_t hash = detail::active_fnv::offset;
  const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < len; ++i) {
    hash ^= static_cast<std::size_t>(bytes[i]);
    hash *= detail::active_fnv::prime;
  }
  return hash;
}

// Boost-style hash combiner. Mixes `value` into `seed` and returns
// the resulting hash. The magic constant is 2^32 / phi for 32-bit
// size_t and is the well-known boost::hash_combine recipe.
METL_NODISCARD inline constexpr std::size_t hash_combine(std::size_t seed, std::size_t value) noexcept {
  seed ^= value + static_cast<std::size_t>(0x9e3779b9) + (seed << 6) + (seed >> 2);
  return seed;
}

namespace detail {

inline std::size_t hash_combine_all_impl(std::size_t seed) noexcept {
  return seed;
}

template <typename T, typename... Rest>
inline std::size_t hash_combine_all_impl(std::size_t seed, const T& value, const Rest&... rest) {
  seed = hash_combine(seed, std::hash<T>{}(value));
  return hash_combine_all_impl(seed, rest...);
}

}  // namespace detail

// Hashes a heterogeneous pack of values by chaining std::hash<T> and
// hash_combine. Returns 0 for an empty pack.
template <typename... Ts>
METL_NODISCARD inline std::size_t hash_combine_all(const Ts&... values) {
  return detail::hash_combine_all_impl(static_cast<std::size_t>(0), values...);
}

// Identity hash for use with integer-keyed open-addressed tables.
// The caller is responsible for ensuring keys have good distribution.
struct identity_hash {
  using is_transparent = void;

  template <typename T>
  METL_NODISCARD constexpr std::size_t operator()(const T& value) const noexcept {
    return static_cast<std::size_t>(value);
  }
};

// Transparent FNV-1a-based hash usable with metl::flat_set / metl::flat_map
// look-up by anything convertible to a contiguous byte range. The default
// overload hashes the object representation; specialize for non-trivial
// types as needed.
struct fnv1a_hash {
  using is_transparent = void;

  template <typename T>
  METL_NODISCARD std::size_t operator()(const T& value) const noexcept {
    // Hashing the raw object representation is only sound when every distinct
    // value has a unique byte pattern. For types with padding bytes,
    // pointers/references, floating point (-0.0 vs +0.0), etc. two equal
    // objects can hash differently, breaking the hash/equality invariant. Gate
    // the raw-bytes overload on has_unique_object_representations; specialize
    // fnv1a_hash for other types.
    static_assert(std::has_unique_object_representations<T>::value,
                  "fnv1a_hash default overload hashes the raw object representation, which is only "
                  "sound for types with a unique object representation (no padding / no ambiguous "
                  "bit patterns). Provide a specialized hash for other types.");
    return fnv1a(reinterpret_cast<const unsigned char*>(&value), sizeof(T));
  }

  METL_NODISCARD std::size_t operator()(const char* str) const noexcept {
    std::size_t len = 0;
    while (str[len] != '\0') {
      ++len;
    }
    return fnv1a(str, len);
  }
};

}  // namespace metl
