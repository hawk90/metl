// Regression tests for `emplace` whose arguments refer into
// the object's own current value. Each block failed against the
// unfixed headers, which destroyed the current member before reading `args`.
//
// `poisoned` writes -1 into itself on destruction and -2 when moved from, so a
// read of a dead or moved-from object shows up as a wrong value without a
// sanitizer (a "destroyed" flag would be a dead store the optimiser removes).
// No exceptions, no heap: runs on QEMU too.

#include "metl_check.hpp"

#include <metl/expected.hpp>
#include <metl/optional.hpp>

namespace {

struct poisoned {
  int value;
  explicit poisoned(int v = 0) noexcept : value(v) {}
  poisoned(const poisoned& o) noexcept : value(o.value) {}
  poisoned(poisoned&& o) noexcept : value(o.value) { o.value = -2; }
  poisoned& operator=(const poisoned& o) noexcept {
    value = o.value;
    return *this;
  }
  poisoned& operator=(poisoned&& o) noexcept {
    value = o.value;
    o.value = -2;
    return *this;
  }
  // Volatile: a plain store to a dying object is a dead store GCC removes at
  // -O2, and a use-after-destroy would then read the old value and pass.
  ~poisoned() { *static_cast<volatile int*>(&value) = -1; }
};

// A value whose subobject is the emplace argument.
struct node {
  int tag;
  poisoned member;
  node(int t, const poisoned& m) noexcept : tag(t), member(m) {}
  explicit node(const poisoned& m) noexcept : tag(0), member(m) {}
};

struct pinned {
  int value;
  explicit pinned(int v) noexcept : value(v) {}
  pinned(const pinned&) = delete;
  pinned(pinned&&) = delete;
  pinned& operator=(const pinned&) = delete;
  pinned& operator=(pinned&&) = delete;
};

// A move that is not noexcept (it never actually throws here): selects the
// backup-and-rollback branch of expected's same-state emplace, which used to
// move the old value into its backup before reading `args`.
struct throwing_move {
  int value = 0;
  throwing_move() = default;
  explicit throwing_move(int v) noexcept : value(v) {}
  throwing_move(const throwing_move& o) noexcept : value(o.value) {}
  throwing_move(throwing_move&& o) noexcept(false) : value(o.value) { o.value = -2; }
  throwing_move& operator=(const throwing_move&) = default;
  throwing_move& operator=(throwing_move&&) = default;
  // Volatile: a plain store to a dying object is a dead store GCC removes at
  // -O2, and a use-after-destroy would then read the old value and pass.
  ~throwing_move() { *static_cast<volatile int*>(&value) = -1; }
};

}  // namespace

int main() {
  // optional: the whole value.
  {
    metl::optional<poisoned> o(metl::in_place, 5);
    o.emplace(*o);
    CHECK_EQ(o->value, 5);
    o.emplace(static_cast<poisoned&&>(*o));
    CHECK_EQ(o->value, 5);
  }
  // optional: a member subobject, alone and with another argument.
  {
    metl::optional<node> o(metl::in_place, 3, poisoned(7));
    o.emplace(o->member);
    CHECK_EQ(o->member.value, 7);
    CHECK_EQ(o->tag, 0);
    o.emplace(o->tag + 4, o->member);
    CHECK_EQ(o->tag, 4);
    CHECK_EQ(o->member.value, 7);
  }
  // optional: disengaged and non-movable paths are unchanged.
  {
    metl::optional<poisoned> o;
    poisoned src(9);
    o.emplace(src);
    CHECK_EQ(o->value, 9);
    metl::optional<pinned> p;
    p.emplace(1);
    p.emplace(2);
    CHECK_EQ(p->value, 2);
  }

  // expected: same-state emplace of the value and of the error.
  {
    metl::expected<poisoned, int> e(metl::in_place, 21);
    e.emplace(e.value());
    CHECK_EQ(e.value().value, 21);
    e.emplace(static_cast<poisoned&&>(*e));
    CHECK_EQ(e->value, 21);
  }
  {
    metl::expected<node, int> e(metl::in_place, 1, poisoned(23));
    e.emplace(e->member);
    CHECK_EQ(e->member.value, 23);
  }
  {
    metl::expected<throwing_move, int> e(metl::in_place, 29);
    e.emplace(e.value());
    CHECK_EQ(e->value, 29);
  }
  {
    metl::expected<int, poisoned> e(metl::unexpect, 25);
    e.emplace_error(e.error());
    CHECK_EQ(e.error().value, 25);
  }
  {
    metl::expected<void, poisoned> e(metl::unexpect, 27);
    e.emplace_error(e.error());
    CHECK_EQ(e.error().value, 27);
  }

  return metl_test::exit_code();
}
