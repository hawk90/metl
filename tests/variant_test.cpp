#include <type_traits>

#include <metl/in_place.hpp>
#include <metl/variant.hpp>

namespace {

struct tracker {
  static int constructions;
  static int destructions;

  tracker() : value(0) { ++constructions; }
  explicit tracker(int input) : value(input) { ++constructions; }
  tracker(const tracker& other) : value(other.value) { ++constructions; }
  tracker(tracker&& other) noexcept : value(other.value) { ++constructions; }
  ~tracker() { ++destructions; }

  tracker& operator=(const tracker& other) {
    value = other.value;
    return *this;
  }

  tracker& operator=(tracker&& other) noexcept {
    value = other.value;
    return *this;
  }

  int value;
};

int tracker::constructions = 0;
int tracker::destructions = 0;

struct two_arg {
  int a;
  int b;
  two_arg(int x, int y) : a(x), b(y) {}
};

bool runtime_checks() {
  metl::variant<int, long> value;
  if (metl::get<int>(value) != 0 || metl::get<0>(value) != 0) {
    return false;
  }

  value.emplace<long>(42L);
  if (metl::get<1>(value) != 42L) {
    return false;
  }

  return metl::visit([](auto input) { return input + 1; }, value) == 43L;
}

}  // namespace

int main() {
  if (!runtime_checks()) {
    return 1;
  }

  metl::variant<int, const char*> value;
  if (!metl::holds_alternative<int>(value) || value.index() != 0) {
    return 2;
  }

  int* initial = metl::get_if<int>(&value);
  if (initial == nullptr || *initial != 0) {
    return 3;
  }

  value = "metl";
  if (!metl::holds_alternative<const char*>(value) || value.index() != 1) {
    return 4;
  }

  const char** text = metl::get_if<const char*>(&value);
  if (text == nullptr || (*text)[0] != 'm') {
    return 5;
  }

  value.emplace<int>(42);
  if (!metl::holds_alternative<int>(value) || *metl::get_if<int>(&value) != 42 ||
      metl::get<int>(value) != 42 || metl::get<0>(value) != 42) {
    return 5;
  }

  int visited = metl::visit(
      [](auto current) {
        using current_type = std::decay_t<decltype(current)>;
        if constexpr (std::is_same_v<current_type, int>) {
          return current;
        } else {
          return current != nullptr ? 99 : 0;
        }
      },
      value);
  if (visited != 42) {
    return 7;
  }

  tracker::constructions = 0;
  tracker::destructions = 0;

  {
    metl::variant<int, tracker> tracked(tracker(7));
    if (!metl::holds_alternative<tracker>(tracked)) {
      return 8;
    }

    tracker* stored = metl::get_if<tracker>(&tracked);
    if (stored == nullptr || stored->value != 7) {
      return 9;
    }

    metl::variant<int, tracker> copied(tracked);
    tracker* copied_value = metl::get_if<tracker>(&copied);
    if (copied_value == nullptr || copied_value->value != 7) {
      return 10;
    }

    const metl::variant<int, tracker>& const_tracked = tracked;
    int visited_tracker = metl::visit(
        [](const auto& current) {
          using current_type = std::decay_t<decltype(current)>;
          if constexpr (std::is_same_v<current_type, int>) {
            return current;
          } else {
            return current.value;
          }
        },
        const_tracked);
    if (visited_tracker != 7) {
      return 11;
    }

    copied.emplace<int>(9);
    if (!metl::holds_alternative<int>(copied) || *metl::get_if<int>(&copied) != 9) {
      return 12;
    }
  }

  if (tracker::constructions != tracker::destructions) {
    return 13;
  }

  // ---- variant_size / variant_alternative ----
  static_assert(metl::variant_size_v<metl::variant<int, long, const char*>> == 3, "size");
  static_assert(std::is_same_v<metl::variant_alternative_t<0, metl::variant<int, long>>, int>, "alt0");
  static_assert(std::is_same_v<metl::variant_alternative_t<1, metl::variant<int, long>>, long>, "alt1");

  // ---- in_place_index / in_place_type ctor ----
  {
    metl::variant<int, long> ip_idx(metl::in_place_index<1>, 99L);
    if (ip_idx.index() != 1 || metl::get<long>(ip_idx) != 99L) {
      return 14;
    }
    metl::variant<int, long> ip_type(metl::in_place_type<int>, 7);
    if (ip_type.index() != 0 || metl::get<int>(ip_type) != 7) {
      return 15;
    }
    metl::variant<int, two_arg> multi(metl::in_place_type<two_arg>, 3, 4);
    if (!metl::holds_alternative<two_arg>(multi)) {
      return 16;
    }
    if (metl::get<two_arg>(multi).a != 3 || metl::get<two_arg>(multi).b != 4) {
      return 17;
    }
  }

  // ---- emplace<I, Args...> ----
  {
    metl::variant<int, two_arg> v(0);
    auto& result = v.emplace<1>(11, 22);
    if (!metl::holds_alternative<two_arg>(v) || result.a != 11 || result.b != 22) {
      return 18;
    }
  }

  // ---- get_if index ----
  {
    metl::variant<int, long> v;
    v.emplace<long>(123L);
    long* p = metl::get_if<1>(&v);
    if (p == nullptr || *p != 123L) {
      return 19;
    }
    int* p2 = metl::get_if<0>(&v);
    if (p2 != nullptr) {
      return 20;
    }
    const metl::variant<int, long>& cv = v;
    const long* cp = metl::get_if<1>(&cv);
    if (cp == nullptr || *cp != 123L) {
      return 21;
    }
  }

  // ---- visit on rvalue ----
  {
    metl::variant<int, long> v;
    v.emplace<int>(5);
    int result = metl::visit([](auto x) -> int { return static_cast<int>(x) * 2; }, std::move(v));
    if (result != 10) {
      return 22;
    }
  }

  // ---- monostate default ctor ----
  {
    metl::variant<metl::monostate, int> v;
    if (v.index() != 0 || !metl::holds_alternative<metl::monostate>(v)) {
      return 23;
    }
    v.emplace<int>(5);
    if (v.index() != 1 || metl::get<int>(v) != 5) {
      return 24;
    }
  }

  // ---- comparison operators ----
  {
    metl::variant<int, long> a(1);
    metl::variant<int, long> b(1);
    metl::variant<int, long> c(2);
    metl::variant<int, long> d;
    d.emplace<long>(1L);
    if (!(a == b))
      return 25;
    if (a == c)
      return 26;
    if (!(a < c))
      return 27;
    if (!(a < d))
      return 28;  // index 0 < index 1
    if (!(a != c))
      return 29;
    if (!(a <= b))
      return 30;
    if (!(c > a))
      return 31;
    if (!(b >= a))
      return 32;
  }

  // ---- conditional noexcept ----
  {
    static_assert(std::is_nothrow_move_constructible_v<metl::variant<int, long>>,
                  "trivial moves should be noexcept");
    static_assert(std::is_nothrow_default_constructible_v<metl::variant<int, long>>,
                  "default ctor should be noexcept when first alt is");
  }

  // ---- valueless_by_exception ----
  {
    metl::variant<int, long> v;
    if (v.valueless_by_exception()) {
      return 33;
    }
  }

  // ---- variant_size_v const/volatile partial specs ----
  static_assert(metl::variant_size_v<const metl::variant<int, long>> == 2, "size const");
  static_assert(metl::variant_size_v<volatile metl::variant<int, long>> == 2, "size volatile");
  static_assert(metl::variant_size_v<const volatile metl::variant<int, long>> == 2, "size cv");
  static_assert(std::is_same_v<metl::variant_alternative_t<0, const metl::variant<int, long>>, const int>,
                "alt const");

  // ---- get<I> rvalue + const ref ----
  {
    metl::variant<int, long> v(metl::in_place_index<1>, 7L);
    long&& r = metl::get<1>(std::move(v));
    if (r != 7L) {
      return 34;
    }
    const metl::variant<int, long>& cv = v;
    const long& cr = metl::get<1>(cv);
    if (cr != 7L) {
      return 35;
    }
  }

  // ---- cross-alternative copy assignment (backup pattern) ----
  {
    tracker::constructions = 0;
    tracker::destructions = 0;
    {
      metl::variant<int, tracker> a(tracker(1));
      metl::variant<int, tracker> b(2);
      a = b;  // a: tracker -> int (cross-alt copy)
      if (!metl::holds_alternative<int>(a) || metl::get<int>(a) != 2) {
        return 36;
      }
    }
    if (tracker::constructions != tracker::destructions) {
      return 37;
    }
  }

  // ---- cross-alternative move assignment ----
  {
    tracker::constructions = 0;
    tracker::destructions = 0;
    {
      metl::variant<int, tracker> a(3);
      metl::variant<int, tracker> b(tracker(99));
      a = std::move(b);  // a: int -> tracker (cross-alt move)
      if (!metl::holds_alternative<tracker>(a) || metl::get<tracker>(a).value != 99) {
        return 38;
      }
    }
    if (tracker::constructions != tracker::destructions) {
      return 39;
    }
  }

  // ---- same-alternative assignment ----
  {
    metl::variant<int, long> a(1);
    metl::variant<int, long> b(2);
    a = b;
    if (metl::get<int>(a) != 2) {
      return 40;
    }
    a.emplace<long>(10L);
    b.emplace<long>(20L);
    a = std::move(b);
    if (metl::get<long>(a) != 20L) {
      return 41;
    }
  }

  // ---- visit returning reference ----
  {
    metl::variant<int, long> v(metl::in_place_type<int>, 5);
    int captured = 0;
    metl::visit([&captured](auto& x) { captured = static_cast<int>(x); }, v);
    if (captured != 5) {
      return 42;
    }
  }

  // ---- emplace<I> returns correct reference ----
  {
    metl::variant<int, two_arg> v(0);
    two_arg& ref = v.emplace<1>(7, 8);
    ref.a = 100;
    if (metl::get<two_arg>(v).a != 100 || metl::get<two_arg>(v).b != 8) {
      return 43;
    }
  }

  // ---- emplace<T> returns correct reference ----
  {
    metl::variant<int, two_arg> v(0);
    two_arg& ref = v.emplace<two_arg>(3, 4);
    ref.b = 50;
    if (metl::get<two_arg>(v).b != 50) {
      return 44;
    }
  }

  // ---- holds_alternative free function ----
  {
    metl::variant<int, long> v(metl::in_place_type<long>, 1L);
    if (!metl::holds_alternative<long>(v) || metl::holds_alternative<int>(v)) {
      return 45;
    }
  }

  // ---- get_if nullptr safety ----
  {
    metl::variant<int, long>* null_ptr = nullptr;
    if (metl::get_if<int>(null_ptr) != nullptr) {
      return 46;
    }
    if (metl::get_if<0>(null_ptr) != nullptr) {
      return 47;
    }
  }

  // ---- monostate comparison ----
  {
    metl::variant<metl::monostate, int> a;
    metl::variant<metl::monostate, int> b;
    if (!(a == b)) {
      return 48;
    }
    if (a != b) {
      return 49;
    }
    if (a < b || a > b) {
      return 50;
    }
    if (!(a <= b) || !(a >= b)) {
      return 51;
    }
  }

  // ---- visit on const rvalue ----
  {
    const metl::variant<int, long> v(metl::in_place_index<0>, 11);
    int result = metl::visit([](auto x) -> int { return static_cast<int>(x) + 1; },
                             static_cast<const metl::variant<int, long>&&>(v));
    if (result != 12) {
      return 52;
    }
  }

  // ---- variant_alternative volatile partial spec ----
  static_assert(
      std::is_same_v<metl::variant_alternative_t<1, volatile metl::variant<int, long>>, volatile long>,
      "alt volatile");
  static_assert(std::is_same_v<metl::variant_alternative_t<0, const volatile metl::variant<int, long>>,
                               const volatile int>,
                "alt cv");

  return 0;
}
