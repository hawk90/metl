#include <utility>

#include <metl/fixed_function.hpp>

namespace {

int add_one(int value) {
  return value + 1;
}

int add_one_nx(int value) noexcept {
  return value + 1;
}

struct offsetter {
  int base;

  int operator()(int value) const { return base + value; }
};

struct counter {
  static int constructions;
  static int destructions;

  counter() : delta(0) { ++constructions; }
  explicit counter(int value) : delta(value) { ++constructions; }
  counter(const counter& other) : delta(other.delta) { ++constructions; }
  counter(counter&& other) noexcept : delta(other.delta) { ++constructions; }
  ~counter() { ++destructions; }

  int operator()(int value) const { return value + delta; }

  int delta;
};

int counter::constructions = 0;
int counter::destructions = 0;

// Move-only callable: deletes copy ops, has explicit move ops. Embedded-safe
// (no heap allocation).
struct move_only_adder {
  int delta;
  bool valid;

  move_only_adder() : delta(0), valid(true) {}
  explicit move_only_adder(int value) : delta(value), valid(true) {}
  move_only_adder(const move_only_adder&) = delete;
  move_only_adder& operator=(const move_only_adder&) = delete;
  move_only_adder(move_only_adder&& other) noexcept : delta(other.delta), valid(other.valid) {
    other.valid = false;
  }
  move_only_adder& operator=(move_only_adder&& other) noexcept {
    delta = other.delta;
    valid = other.valid;
    other.valid = false;
    return *this;
  }

  int operator()(int value) const { return value + delta; }
};

}  // namespace

int main() {
  // ---- baseline regression suite -------------------------------------------
  metl::fixed_function<int(int), 16> function;
  if (function.has_value()) {
    return 1;
  }

  function = add_one;
  if (!function || function(3) != 4) {
    return 2;
  }

  offsetter plus_five{5};
  function = plus_five;
  if (function(7) != 12) {
    return 3;
  }

  metl::fixed_function<int(int), 16> copied(function);
  if (!copied || copied(2) != 7) {
    return 4;
  }

  metl::fixed_function<int(int), 16> moved(static_cast<metl::fixed_function<int(int), 16>&&>(copied));
  if (!moved || moved(4) != 9) {
    return 5;
  }

  counter::constructions = 0;
  counter::destructions = 0;

  {
    metl::fixed_function<int(int), 16> tracked;
    tracked.assign(counter(9));
    if (tracked(1) != 10) {
      return 6;
    }

    tracked.reset();
    if (tracked.has_value()) {
      return 7;
    }
  }

  if (counter::constructions != counter::destructions) {
    return 8;
  }

  struct too_large {
    char payload[64];
    int operator()(int value) const { return value; }
  };

  metl::fixed_function<int(int), 16> small;
  if (small.try_assign(too_large{})) {
    return 9;
  }

  // ---- nullptr comparison --------------------------------------------------
  metl::fixed_function<int(int), 16> empty;
  if (!(empty == nullptr)) {
    return 10;
  }
  if (empty != nullptr) {
    return 11;
  }
  if (!(nullptr == empty)) {
    return 12;
  }
  empty = add_one;
  if (empty == nullptr) {
    return 13;
  }
  if (!(empty != nullptr)) {
    return 14;
  }
  empty = nullptr;
  if (empty != nullptr) {
    return 15;
  }

  // ---- swap (member and free) ----------------------------------------------
  {
    metl::fixed_function<int(int), 16> a;
    metl::fixed_function<int(int), 16> b;
    a = offsetter{10};
    b = offsetter{20};
    a.swap(b);
    if (a(1) != 21 || b(1) != 11) {
      return 16;
    }
    using std::swap;
    swap(a, b);
    if (a(1) != 11 || b(1) != 21) {
      return 17;
    }
    // swap empty <-> filled
    metl::fixed_function<int(int), 16> c;
    metl::fixed_function<int(int), 16> d;
    d = offsetter{7};
    swap(c, d);
    if (!c || d || c(0) != 7) {
      return 18;
    }
  }

  // ---- noexcept signature specialization -----------------------------------
  {
    metl::fixed_function<int(int) noexcept, 16> nx;
    if (nx) {
      return 19;
    }
    nx = add_one_nx;
    if (!nx || nx(5) != 6) {
      return 20;
    }

    // A C++ lambda with noexcept call operator must be acceptable.
    auto nx_lambda = [](int v) noexcept -> int { return v * 2; };
    nx = nx_lambda;
    if (nx(4) != 8) {
      return 21;
    }

    // Copy/move/nullptr behaviour for the noexcept variant.
    metl::fixed_function<int(int) noexcept, 16> nx_copy(nx);
    if (nx_copy(3) != 6) {
      return 22;
    }
    nx_copy = nullptr;
    if (nx_copy != nullptr) {
      return 23;
    }
  }

  // ---- fixed_any_invocable: move-only callable -----------------------------
  {
    metl::fixed_any_invocable<int(int), 32> any;
    if (any) {
      return 24;
    }

    any = move_only_adder{11};
    if (!any || any(5) != 16) {
      return 25;
    }

    // Move construction.
    metl::fixed_any_invocable<int(int), 32> any_moved(std::move(any));
    if (!any_moved || any_moved(1) != 12) {
      return 26;
    }
    if (any) {
      return 27;  // moved-from should be empty
    }

    // Move assignment.
    metl::fixed_any_invocable<int(int), 32> any_target;
    any_target = std::move(any_moved);
    if (!any_target || any_target(0) != 11) {
      return 28;
    }

    // nullptr comparison
    if (any_target == nullptr) {
      return 29;
    }
    any_target = nullptr;
    if (any_target != nullptr) {
      return 30;
    }

    // try_assign with move-only callable should also succeed.
    metl::fixed_any_invocable<int(int), 32> any_assigned;
    if (!any_assigned.try_assign(move_only_adder{3})) {
      return 31;
    }
    if (any_assigned(4) != 7) {
      return 32;
    }

    // try_assign with too-large callable returns false.
    struct big_move_only {
      char pad[64];
      int extra;

      big_move_only() : extra(0) {}
      big_move_only(const big_move_only&) = delete;
      big_move_only& operator=(const big_move_only&) = delete;
      big_move_only(big_move_only&&) noexcept = default;
      big_move_only& operator=(big_move_only&&) noexcept = default;

      int operator()(int v) const { return v; }
    };
    metl::fixed_any_invocable<int(int), 16> tight;
    if (tight.try_assign(big_move_only{})) {
      return 33;
    }
  }

  // ---- fixed_any_invocable: noexcept signature -----------------------------
  {
    metl::fixed_any_invocable<int(int) noexcept, 32> any_nx;
    auto nx_lambda = [](int v) noexcept -> int { return v + 100; };
    any_nx = nx_lambda;
    if (any_nx(1) != 101) {
      return 34;
    }

    // Move-only noexcept callable
    struct mo_nx {
      int v;
      explicit mo_nx(int value) : v(value) {}
      mo_nx(const mo_nx&) = delete;
      mo_nx& operator=(const mo_nx&) = delete;
      mo_nx(mo_nx&&) noexcept = default;
      mo_nx& operator=(mo_nx&&) noexcept = default;
      int operator()(int x) const noexcept { return x + v; }
    };
    metl::fixed_any_invocable<int(int) noexcept, 32> any_nx_mo;
    any_nx_mo = mo_nx{50};
    if (any_nx_mo(2) != 52) {
      return 35;
    }
  }

  // ---- fixed_any_invocable: swap -------------------------------------------
  {
    metl::fixed_any_invocable<int(int), 32> a;
    metl::fixed_any_invocable<int(int), 32> b;
    a = move_only_adder{1};
    b = move_only_adder{2};
    a.swap(b);
    if (a(0) != 2 || b(0) != 1) {
      return 36;
    }
    using std::swap;
    swap(a, b);
    if (a(0) != 1 || b(0) != 2) {
      return 37;
    }
  }

  return 0;
}
