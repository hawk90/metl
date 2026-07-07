#pragma once

#include <cstddef>

namespace metl {

struct in_place_t {
  explicit constexpr in_place_t() = default;
};
inline constexpr in_place_t in_place{};

template <typename T>
struct in_place_type_t {
  explicit constexpr in_place_type_t() = default;
};
template <typename T>
inline constexpr in_place_type_t<T> in_place_type{};

template <std::size_t I>
struct in_place_index_t {
  explicit constexpr in_place_index_t() = default;
};
template <std::size_t I>
inline constexpr in_place_index_t<I> in_place_index{};

struct unexpect_t {
  explicit constexpr unexpect_t() = default;
};
inline constexpr unexpect_t unexpect{};

struct nullopt_t {
  struct private_tag {};
  explicit constexpr nullopt_t(private_tag) noexcept {}
};
inline constexpr nullopt_t nullopt{nullopt_t::private_tag{}};

struct monostate {};

constexpr bool operator==(monostate, monostate) noexcept {
  return true;
}
constexpr bool operator!=(monostate, monostate) noexcept {
  return false;
}
constexpr bool operator<(monostate, monostate) noexcept {
  return false;
}
constexpr bool operator>(monostate, monostate) noexcept {
  return false;
}
constexpr bool operator<=(monostate, monostate) noexcept {
  return true;
}
constexpr bool operator>=(monostate, monostate) noexcept {
  return true;
}

}  // namespace metl
