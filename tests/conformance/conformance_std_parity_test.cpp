// Property: where a type has a std counterpart, what it offers agrees (#391).
//
//   agrees_with_std   with the same elements added, the type and its std
//                     counterpart iterate in the same order (as a set for the
//                     unordered types); every comparison the type offers --
//                     ==, !=, <, <=, >, >=, and <=> in C++20 -- gives the same
//                     answer for every pair of fills; and erase(iterator) at
//                     every position leaves the same elements and returns the
//                     element that followed, or end().
//
// Only what the type offers is compared: METL does not copy the whole std
// interface (the queues have no comparisons, for one), and a missing operation
// is not a mismatch. The counterpart of each registered type is named below,
// or the reason it has none; a new adapter fails to compile until it is one
// or the other.

#include "conformance.hpp"

#include <algorithm>
#include <deque>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#if defined(__cpp_impl_three_way_comparison) && defined(__cpp_lib_three_way_comparison)
#include <compare>
#endif

METL_CONFORMANCE_DECLARE_GAP(agrees_with_std)

namespace {

using conformance::probe;

// ---- The counterparts ---------------------------------------------------------

// `type` and `add`, or `refused` with the reason there is none.
template <typename Adapter>
struct counterpart;

template <typename Std>
struct emplace_back_counterpart {
  using type = Std;
  static void add(type& s, int id) { s.emplace_back(id); }
};
template <typename Std>
struct emplace_counterpart {
  using type = Std;
  static void add(type& s, int id) { s.emplace(id); }
};
template <typename Std>
struct keyed_counterpart {
  using type = Std;
  static void add(type& s, int id) { s.emplace(id, probe(id)); }
};
struct refused_counterpart {
  const char* why;
};

template <>
struct counterpart<conformance::fixed_vector_adapter> : emplace_back_counterpart<std::vector<probe>> {};
template <>
struct counterpart<conformance::fixed_deque_adapter> : emplace_back_counterpart<std::deque<probe>> {};
template <>
struct counterpart<conformance::flat_map_adapter> : keyed_counterpart<std::map<int, probe>> {};
template <>
struct counterpart<conformance::flat_set_adapter> : emplace_counterpart<std::set<probe>> {};
template <>
struct counterpart<conformance::static_unordered_map_adapter>
    : keyed_counterpart<std::unordered_map<int, probe>> {};
template <>
struct counterpart<conformance::static_unordered_set_adapter>
    : emplace_counterpart<std::unordered_set<probe, conformance::probe_hash>> {};
template <>
struct counterpart<conformance::optional_adapter> : emplace_counterpart<std::optional<probe>> {};
template <>
struct counterpart<conformance::variant_adapter> {
  using type = std::variant<int, probe>;
  static void add(type& s, int id) { s.template emplace<probe>(id); }
};

#define METL_NO_COUNTERPART(adapter, reason)       \
  template <>                                      \
  struct counterpart<conformance::adapter> {       \
    static constexpr const char* refused = reason; \
  };
METL_NO_COUNTERPART(ring_buffer_adapter, "the standard library has no ring buffer")
METL_NO_COUNTERPART(fixed_queue_adapter,
                    "it offers no comparison, iteration or erase to compare with std::queue")
METL_NO_COUNTERPART(fixed_stack_adapter,
                    "it offers no comparison, iteration or erase to compare with std::stack")
METL_NO_COUNTERPART(fixed_priority_queue_adapter,
                    "it offers no comparison, iteration or erase to compare with std::priority_queue")
METL_NO_COUNTERPART(static_message_queue_adapter, "the standard library has no message queue")
METL_NO_COUNTERPART(object_pool_adapter, "the standard library has no object pool")
METL_NO_COUNTERPART(handle_pool_adapter, "the standard library has no handle pool")
METL_NO_COUNTERPART(arena_allocator_adapter, "the standard library has no arena")
METL_NO_COUNTERPART(spsc_queue_adapter, "the standard library has no lock-free queue")
METL_NO_COUNTERPART(mpmc_queue_adapter, "the standard library has no lock-free queue")
METL_NO_COUNTERPART(expected_adapter, "std::expected is C++23, and METL builds C++17 and C++20")
METL_NO_COUNTERPART(fixed_function_adapter,
                    "std::function compares only with nullptr, which fixed_function does not")
#undef METL_NO_COUNTERPART

template <typename A, typename = void>
struct has_refused : std::false_type {};
template <typename A>
struct has_refused<A, std::void_t<decltype(counterpart<A>::refused)>> : std::true_type {};

// ---- What the type offers -------------------------------------------------------

template <typename T, typename = void>
struct has_equal : std::false_type {};
template <typename T>
struct has_equal<T, std::void_t<decltype(std::declval<const T&>() == std::declval<const T&>())>>
    : std::true_type {};
template <typename T, typename = void>
struct has_less : std::false_type {};
template <typename T>
struct has_less<T, std::void_t<decltype(std::declval<const T&>() < std::declval<const T&>())>>
    : std::true_type {};
#if defined(__cpp_impl_three_way_comparison) && defined(__cpp_lib_three_way_comparison)
// Standard: c++17 would split the operator into two tokens.
// clang-format off
template <typename T, typename = void>
struct has_spaceship : std::false_type {};
template <typename T>
struct has_spaceship<T, std::void_t<decltype(std::declval<const T&>() <=> std::declval<const T&>())>>
    : std::true_type {};
// clang-format on
#endif
template <typename T, typename = void>
struct iterable : std::false_type {};
template <typename T>
struct iterable<T, std::void_t<decltype(std::declval<T&>().begin())>> : std::true_type {};
template <typename T, typename = void>
struct has_erase_iterator : std::false_type {};
template <typename T>
struct has_erase_iterator<
    T,
    std::enable_if_t<std::is_same_v<decltype(std::declval<T&>().erase(std::declval<T&>().begin())),
                                    decltype(std::declval<T&>().begin())>>> : std::true_type {};

// The id an element carries: a probe's own, or a map entry's key.
template <typename E, typename = void>
struct has_key_member : std::false_type {};
template <typename E>
struct has_key_member<E, std::void_t<decltype(std::declval<const E&>().key)>>
    : std::is_convertible<decltype(std::declval<const E&>().key), int> {};  // not probe::key()
template <typename E, typename = void>
struct has_first_member : std::false_type {};
template <typename E>
struct has_first_member<E, std::void_t<decltype(std::declval<const E&>().first)>> : std::true_type {};
template <typename E>
int id_of(const E& e) {
  if constexpr (has_key_member<E>::value) {
    return e.key;
  } else if constexpr (has_first_member<E>::value) {
    return e.first;
  } else {
    return e.id;
  }
}

template <typename C>
std::vector<int> ids(C& c) {
  std::vector<int> out;
  for (auto it = c.begin(); it != c.end(); ++it) {
    out.push_back(id_of(*it));
  }
  return out;
}

// ---- The fills ---------------------------------------------------------------------

// The ids the fills draw from: 1, 2 and 3, or the adapter's own (`parity_ids`).
template <typename A, typename = void>
struct has_parity_ids : std::false_type {};
template <typename A>
struct has_parity_ids<A, std::void_t<decltype(A::parity_ids())>> : std::true_type {};
template <typename Adapter>
std::array<int, 3> parity_ids() {
  if constexpr (has_parity_ids<Adapter>::value) {
    return Adapter::parity_ids();
  } else {
    return {1, 2, 3};
  }
}

// Every sequence of up to `capacity` of those ids, empty first.
template <typename Adapter>
std::vector<std::vector<int>> fills() {
  std::vector<std::vector<int>> out{{}};
  const int longest = static_cast<int>(Adapter::capacity < 3 ? Adapter::capacity : 3);
  const std::array<int, 3> ids = parity_ids<Adapter>();
  for (int length = 1; length <= longest; ++length) {
    std::vector<int> f(static_cast<std::size_t>(length), 1);
    for (;;) {
      std::vector<int> drawn;
      for (int index : f) {
        drawn.push_back(ids[static_cast<std::size_t>(index - 1)]);
      }
      out.push_back(drawn);
      int i = length - 1;
      while (i >= 0 && f[static_cast<std::size_t>(i)] == 3) {
        f[static_cast<std::size_t>(i--)] = 1;
      }
      if (i < 0) {
        break;
      }
      ++f[static_cast<std::size_t>(i)];
    }
  }
  return out;
}

// A unique or single-element type ignores an id it already holds, as its std
// counterpart does, so only distinct ids are added to either.
template <typename Adapter>
void fill(typename Adapter::type& c,
          typename counterpart<Adapter>::type& s,
          const std::vector<int>& ids_to_add) {
  for (int id : ids_to_add) {
    if (Adapter::try_add(c, id)) {
      counterpart<Adapter>::add(s, id);
    }
  }
}

struct agrees_with_std {
  template <typename Adapter>
  bool operator()(Adapter) const {
    using type = typename Adapter::type;
    using std_type = typename counterpart<Adapter>::type;
    bool met = true;
    const auto all = fills<Adapter>();
    for (const auto& x : all) {
      // Iteration: the same elements, in the same order where the order is
      // defined.
      if constexpr (iterable<type>::value) {
        type c;
        std_type s;
        fill<Adapter>(c, s, x);
        std::vector<int> mine = ids(c);
        std::vector<int> theirs = ids(s);
        if (std::is_same_v<std_type, std::unordered_map<int, probe>> ||
            std::is_same_v<std_type, std::unordered_set<probe, conformance::probe_hash>>) {
          std::sort(mine.begin(), mine.end());
          std::sort(theirs.begin(), theirs.end());
        }
        if (mine != theirs) {
          std::fprintf(stderr, "  %s: iterates differently from its std counterpart\n", Adapter::name);
          met = false;
        }
        // erase(iterator) at every position: the same elements are left, and
        // the result is the element that followed the erased one.
        if constexpr (has_erase_iterator<type>::value) {
          for (std::size_t p = 0; p < mine.size(); ++p) {
            type e;
            std_type es;
            fill<Adapter>(e, es, x);
            auto it = e.begin();
            std::advance(it, static_cast<std::ptrdiff_t>(p));
            const int erased_id = id_of(*it);
            auto next = it;
            ++next;
            const bool next_is_end = next == e.end();
            const int next_id = next_is_end ? -1 : id_of(*next);
            auto returned = e.erase(it);
            const bool right_result =
                next_is_end ? returned == e.end() : returned != e.end() && id_of(*returned) == next_id;
            // The counterpart erases the element with the same id.
            auto sit = es.begin();
            while (sit != es.end() && id_of(*sit) != erased_id) {
              ++sit;
            }
            if (sit != es.end()) {
              es.erase(sit);
            }
            std::vector<int> left = ids(e);
            std::vector<int> std_left = ids(es);
            std::sort(left.begin(), left.end());
            std::sort(std_left.begin(), std_left.end());
            if (!right_result || left != std_left) {
              std::fprintf(stderr,
                           "  %s: erase(iterator) at %zu of %zu returned %s\n",
                           Adapter::name,
                           p,
                           mine.size(),
                           right_result ? "the right element but left different ones" : "the wrong element");
              met = false;
            }
            Adapter::clear(e);
          }
        }
        Adapter::clear(c);
      }
      // Comparisons, against every other fill.
      for (const auto& y : all) {
        type a;
        type b;
        std_type sa;
        std_type sb;
        fill<Adapter>(a, sa, x);
        fill<Adapter>(b, sb, y);
        bool same = true;
        if constexpr (has_equal<type>::value) {
          same = same && (a == b) == (sa == sb) && (a != b) == (sa != sb);
        }
        if constexpr (has_less<type>::value) {
          same = same && (a < b) == (sa < sb) && (a <= b) == (sa <= sb) && (a > b) == (sa > sb) &&
                 (a >= b) == (sa >= sb);
        }
#if defined(__cpp_impl_three_way_comparison) && defined(__cpp_lib_three_way_comparison)
        // clang-format off
        if constexpr (has_spaceship<type>::value) {
          same = same && std::is_lt(a <=> b) == std::is_lt(sa <=> sb) &&
                 std::is_eq(a <=> b) == std::is_eq(sa <=> sb);
        }
        // clang-format on
#endif
        if (!same) {
          std::fprintf(stderr, "  %s: a comparison disagrees with its std counterpart\n", Adapter::name);
          met = false;
        }
        Adapter::clear(a);
        Adapter::clear(b);
      }
    }
    probe::live() = 0;
    return met;
  }
};

template <typename Adapter>
void run_for() {
  if constexpr (has_refused<Adapter>::value) {
    std::printf("%s: agrees_with_std: not applicable: %s\n", Adapter::name, counterpart<Adapter>::refused);
  } else {
    conformance::run_one<Adapter>(
        "agrees_with_std", METL_CONFORMANCE_GAP(agrees_with_std, Adapter), agrees_with_std{});
  }
}

template <typename... Adapters>
void run_all(std::tuple<Adapters...>*) {
  (run_for<Adapters>(), ...);
}

}  // namespace

int main() {
  run_all(static_cast<conformance::registry*>(nullptr));
  return metl_test::exit_code();
}
