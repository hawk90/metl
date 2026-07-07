#include <array>
#include <cstddef>
#include <type_traits>

#include <metl/span.hpp>

namespace {

constexpr bool static_checks() {
  int values[] = {1, 2, 3, 4};
  metl::span<int> view(values);

  if (view.size() != 4) {
    return false;
  }

  if (view.front() != 1 || view.back() != 4) {
    return false;
  }

  auto middle = view.subspan(1, 2);
  if (middle.size() != 2 || middle[0] != 2 || middle[1] != 3) {
    return false;
  }

  auto prefix = view.first(2);
  auto suffix = view.last(2);

  return prefix[1] == 2 && suffix[0] == 3;
}

static_assert(static_checks(), "span constexpr checks failed");

// Compile-time checks for fixed-extent span.
constexpr bool fixed_extent_checks() {
  int values[] = {10, 20, 30, 40, 50};
  metl::span<int, 5> fixed(values);

  static_assert(decltype(fixed)::extent == 5, "extent must be 5");

  if (fixed.size() != 5) {
    return false;
  }

  // Templated first<N>() returns fixed-extent span.
  auto head = fixed.first<2>();
  static_assert(decltype(head)::extent == 2, "first<2>() extent must be 2");
  if (head.size() != 2 || head[0] != 10 || head[1] != 20) {
    return false;
  }

  // Templated last<N>().
  auto tail = fixed.last<2>();
  static_assert(decltype(tail)::extent == 2, "last<2>() extent must be 2");
  if (tail[0] != 40 || tail[1] != 50) {
    return false;
  }

  // Templated subspan<Offset, Count>().
  auto mid = fixed.subspan<1, 3>();
  static_assert(decltype(mid)::extent == 3, "subspan<1,3>() extent must be 3");
  if (mid.size() != 3 || mid[0] != 20 || mid[2] != 40) {
    return false;
  }

  // Templated subspan<Offset>() — Count defaults to Extent-Offset.
  auto suffix = fixed.subspan<2>();
  static_assert(decltype(suffix)::extent == 3, "subspan<2>() extent must be Extent-2 = 3");
  if (suffix.size() != 3 || suffix[0] != 30) {
    return false;
  }

  return true;
}

static_assert(fixed_extent_checks(), "fixed extent span checks failed");

// Storage size: fixed-extent should be pointer-sized only.
static_assert(sizeof(metl::span<int, 4>) == sizeof(int*), "fixed-extent span must store data only");
static_assert(sizeof(metl::span<int>) >= sizeof(int*) + sizeof(std::size_t),
              "dynamic-extent span stores data + size");

// dynamic_extent value.
static_assert(metl::dynamic_extent == static_cast<std::size_t>(-1), "dynamic_extent sentinel");

// CTAD: from C-array.
constexpr bool ctad_checks() {
  int arr[] = {1, 2, 3};
  metl::span ctad_arr{arr};
  static_assert(std::is_same<decltype(ctad_arr), metl::span<int, 3>>::value,
                "CTAD from array deduces fixed extent");
  return ctad_arr.size() == 3;
}

static_assert(ctad_checks(), "CTAD checks failed");

// Cross-extent conversion: fixed -> dynamic.
constexpr bool cross_extent_checks() {
  int arr[] = {7, 8, 9};
  metl::span<int, 3> fixed(arr);
  metl::span<int> dyn(fixed);
  if (dyn.size() != 3 || dyn[1] != 8) {
    return false;
  }
  // int -> const int (qualification conversion).
  metl::span<const int, 3> cfixed(fixed);
  return cfixed[2] == 9;
}

static_assert(cross_extent_checks(), "cross-extent checks failed");

}  // namespace

int main() {
  int values[] = {10, 20, 30};
  metl::span<int> writable(values);
  metl::span<const int> readable(writable);

  if (readable.size_bytes() != sizeof(values)) {
    return 1;
  }

  writable[1] = 99;

  if (values[1] != 99) {
    return 2;
  }

  std::size_t sum = 0;
  for (int value : readable) {
    sum += static_cast<std::size_t>(value);
  }

  if (sum != 139) {
    return 3;
  }

  // Fixed-extent runtime use.
  int fixed_values[] = {1, 2, 3, 4, 5};
  metl::span<int, 5> fixed(fixed_values);

  if (fixed.size() != 5 || fixed.extent != 5) {
    return 4;
  }

  // Reverse iterators.
  int reverse_sum = 0;
  for (auto it = fixed.rbegin(); it != fixed.rend(); ++it) {
    reverse_sum = reverse_sum * 10 + *it;
  }
  if (reverse_sum != 54321) {
    return 5;
  }

  // as_bytes.
  auto bytes = metl::as_bytes(fixed);
  if (bytes.size() != sizeof(fixed_values)) {
    return 6;
  }
  static_assert(std::is_same<decltype(bytes)::element_type, const std::byte>::value,
                "as_bytes yields const byte");
  static_assert(decltype(bytes)::extent == sizeof(int) * 5, "as_bytes preserves fixed extent");

  // as_writable_bytes.
  auto wbytes = metl::as_writable_bytes(fixed);
  if (wbytes.size() != sizeof(fixed_values)) {
    return 7;
  }
  static_assert(std::is_same<decltype(wbytes)::element_type, std::byte>::value,
                "as_writable_bytes yields mutable byte");

  // Mutate via bytes — write zero into first int.
  for (std::size_t i = 0; i < sizeof(int); ++i) {
    wbytes[i] = std::byte{0};
  }
  if (fixed_values[0] != 0) {
    return 8;
  }

  // Templated subspan returning fixed extent on dynamic source.
  metl::span<int> dyn_view(fixed_values);
  auto sub = dyn_view.subspan<1, 2>();
  static_assert(decltype(sub)::extent == 2, "subspan<1,2> on dynamic still yields fixed extent 2");
  if (sub.size() != 2 || sub[0] != 2 || sub[1] != 3) {
    return 9;
  }

  // CTAD from std::array via data()/size() container ctor.
  std::array<int, 4> stdarr{100, 200, 300, 400};
  metl::span<int> from_array(stdarr);
  if (from_array.size() != 4 || from_array[3] != 400) {
    return 10;
  }

  // as_bytes on dynamic-extent span yields dynamic-extent.
  auto dyn_bytes = metl::as_bytes(metl::span<int>(fixed_values));
  static_assert(decltype(dyn_bytes)::extent == metl::dynamic_extent, "dynamic span -> dynamic bytes");
  if (dyn_bytes.size() != sizeof(fixed_values)) {
    return 11;
  }

  // Default ctor for Extent==0.
  metl::span<int, 0> empty_fixed;
  if (!empty_fixed.empty() || empty_fixed.size() != 0) {
    return 12;
  }

  // Default ctor for dynamic_extent.
  metl::span<int> empty_dyn;
  if (!empty_dyn.empty()) {
    return 13;
  }

  return 0;
}
