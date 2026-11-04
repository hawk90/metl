// The copy, move and swap traits of every container and vocabulary type follow
// its element (#601).
//
// Each type used to declare its copy and move operations for any element, so
// `std::is_copy_constructible_v<fixed_vector<U, 2>>` was true for a `U` that
// cannot be copied, and the error appeared only once the body was instantiated.
// Code that asks the trait first -- `std::optional` of a container, a
// `variant`, a generic algorithm -- got a hard error instead of a clean false.
// Now each operation exists only when its element allows it
// (detail/copy_move_gate.hpp). `std::array` and `std::optional` are the model.

#include "metl_deprecated.hpp"  // still tests metl::variant (#635)

#include <cstddef>
#include <functional>
#include <optional>
#include <type_traits>

#include <metl/expected.hpp>
#include <metl/fixed_deque.hpp>
#include <metl/fixed_priority_queue.hpp>
#include <metl/fixed_queue.hpp>
#include <metl/fixed_stack.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/optional.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>
#include <metl/variant.hpp>

namespace {

// Neither copyable nor movable.
struct pinned {
  explicit pinned(int) {}
  pinned(const pinned&) = delete;
  pinned(pinned&&) = delete;
  pinned& operator=(const pinned&) = delete;
  pinned& operator=(pinned&&) = delete;
  ~pinned() = default;
  bool operator<(const pinned&) const { return false; }
  bool operator==(const pinned&) const { return true; }
};

// Movable, not copyable.
struct move_only {
  explicit move_only(int) {}
  move_only(const move_only&) = delete;
  move_only(move_only&&) = default;
  move_only& operator=(const move_only&) = delete;
  move_only& operator=(move_only&&) = default;
  ~move_only() = default;
  bool operator<(const move_only&) const { return false; }
  bool operator==(const move_only&) const { return true; }
};

struct any_hash {
  template <typename T>
  std::size_t operator()(const T&) const noexcept {
    return 0;
  }
};

template <typename C>
constexpr bool none =
    !std::is_copy_constructible_v<C> && !std::is_move_constructible_v<C> && !std::is_copy_assignable_v<C> &&
    !std::is_move_assignable_v<C> && !std::is_swappable_v<C>;

template <typename C>
constexpr bool move_only_shape =
    !std::is_copy_constructible_v<C> && std::is_move_constructible_v<C> && !std::is_copy_assignable_v<C> &&
    std::is_move_assignable_v<C> && std::is_swappable_v<C>;

template <typename C>
constexpr bool all = std::is_copy_constructible_v<C> && std::is_move_constructible_v<C> &&
                     std::is_copy_assignable_v<C> && std::is_move_assignable_v<C> && std::is_swappable_v<C>;

// One alias per type, so each line below reads as "this type, of this element".
template <typename T>
using vector_of = metl::fixed_vector<T, 2>;
template <typename T>
using ring_of = metl::ring_buffer<T, 2>;
template <typename T>
using deque_of = metl::fixed_deque<T, 2>;
template <typename T>
using stack_of = metl::fixed_stack<T, 2>;
template <typename T>
using queue_of = metl::fixed_queue<T, 2>;
template <typename T>
using map_of = metl::flat_map<int, T, 2>;
template <typename T>
using set_of = metl::flat_set<T, 2>;
template <typename T>
using unordered_map_of = metl::static_unordered_map<int, T, 2>;
template <typename T>
using unordered_set_of = metl::static_unordered_set<T, 2, any_hash>;
template <typename T>
using optional_of = metl::optional<T>;
template <typename T>
using expected_of = metl::expected<T, int>;
template <typename T>
using error_of = metl::expected<void, T>;
template <typename T>
using variant_of = metl::variant<int, T>;

#define METL_TRAITS_ROW(alias)                                                            \
  static_assert(none<alias<pinned>>, #alias ": neither copyable nor movable");            \
  static_assert(move_only_shape<alias<move_only>>, #alias ": move-only, as its element"); \
  static_assert(all<alias<int>>, #alias ": copyable and movable, as its element");

METL_TRAITS_ROW(vector_of)
METL_TRAITS_ROW(ring_of)
METL_TRAITS_ROW(deque_of)
METL_TRAITS_ROW(stack_of)
METL_TRAITS_ROW(queue_of)
METL_TRAITS_ROW(map_of)
METL_TRAITS_ROW(set_of)
METL_TRAITS_ROW(unordered_map_of)
METL_TRAITS_ROW(unordered_set_of)
METL_TRAITS_ROW(optional_of)
METL_TRAITS_ROW(expected_of)
METL_TRAITS_ROW(error_of)
METL_TRAITS_ROW(variant_of)

#undef METL_TRAITS_ROW

// fixed_priority_queue rejects an element it cannot move by static_assert, so
// only the move-only and copyable rows apply.
static_assert(move_only_shape<metl::fixed_priority_queue<move_only, 2>>, "fixed_priority_queue: move-only");
static_assert(all<metl::fixed_priority_queue<int, 2>>, "fixed_priority_queue: copyable");

// The point of the trait: code that asks it gets a clean answer. std::optional
// of a container of pinned elements is itself neither copyable nor movable,
// instead of failing to compile when the question is asked.
static_assert(!std::is_copy_constructible_v<std::optional<vector_of<pinned>>>, "");
static_assert(std::is_move_constructible_v<std::optional<vector_of<move_only>>>, "");

// A stateful comparator, hasher or key-equal that cannot be copied makes its
// container move-only, as an element would.
struct move_only_less {
  move_only_less() = default;
  move_only_less(const move_only_less&) = delete;
  move_only_less(move_only_less&&) = default;
  move_only_less& operator=(const move_only_less&) = delete;
  move_only_less& operator=(move_only_less&&) = default;
  ~move_only_less() = default;
  bool operator()(int a, int b) const { return a < b; }
};
static_assert(move_only_shape<metl::flat_map<int, int, 2, move_only_less>>,
              "flat_map: comparator decides too");
static_assert(move_only_shape<metl::flat_set<int, 2, move_only_less>>, "flat_set: comparator decides too");

}  // namespace

int main() {
  // Copying still works where it is allowed: the gated operations are the same
  // functions as before for a copyable element.
  vector_of<int> a;
  a.push_back(1);
  vector_of<int> b(a);
  vector_of<int> c;
  c = b;
  metl::optional<int> o(3);
  metl::optional<int> p(o);
  return (c.size() == 1 && c[0] == 1 && p.has_value() && *p == 3) ? 0 : 1;
}
