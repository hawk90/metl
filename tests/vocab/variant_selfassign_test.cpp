// Regression tests for the converting variant::operator=(T&&) fix:
// when the active alternative already holds the target type, the assignment must
// happen IN PLACE, not via reset()+reconstruct. The old code always routed
// through emplace(), which destroyed the active alternative BEFORE reading the
// (possibly aliasing) source — a use-after-destruction for `v = get<T>(v)`.
#include "metl_check.hpp"

#include <metl/variant.hpp>

namespace {

struct Tracked {
  inline static int ctors = 0;
  inline static int dtors = 0;
  inline static int copy_assigns = 0;
  inline static int move_assigns = 0;
  static void reset_counts() { ctors = dtors = copy_assigns = move_assigns = 0; }

  int value;

  explicit Tracked(int v = 0) : value(v) { ++ctors; }
  Tracked(const Tracked& other) : value(other.value) { ++ctors; }
  Tracked(Tracked&& other) noexcept : value(other.value) { ++ctors; }
  Tracked& operator=(const Tracked& other) {
    value = other.value;
    ++copy_assigns;
    return *this;
  }
  Tracked& operator=(Tracked&& other) noexcept {
    value = other.value;
    ++move_assigns;
    return *this;
  }
  ~Tracked() { ++dtors; }
};

}  // namespace

int main() {
  // (1) Same-alternative assignment must assign IN PLACE (call T::operator=),
  // not destroy-then-reconstruct. The distinguishing signal is that an
  // assignment operator runs: the old emplace()-always path would instead do a
  // destructor + constructor and leave move_assigns/copy_assigns at zero.
  {
    Tracked::reset_counts();
    metl::variant<int, Tracked> v{Tracked{1}};
    const int ctors_before = Tracked::ctors;
    v = Tracked{2};  // active alternative is already Tracked → in-place assign
    CHECK_EQ(metl::get<Tracked>(v).value, 2);
    CHECK(Tracked::move_assigns >= 1);  // in-place assignment happened
    // No new alternative object was constructed for the active slot (only the
    // RHS temporary, which is not the active alternative).
    CHECK_EQ(Tracked::ctors, ctors_before + 1);  // just the RHS temporary
  }

  // (2) Self-aliasing assignment (`v = get<T>(v)`) must be safe and preserve the
  // value. Under the old destroy-then-read code this was a use-after-destruction.
  {
    metl::variant<int, Tracked> v{Tracked{42}};
    v = metl::get<Tracked>(v);  // RHS aliases the active alternative
    CHECK_EQ(metl::get<Tracked>(v).value, 42);
    CHECK_EQ(v.index(), 1u);
  }

  // (3) Cross-type assignment still switches the alternative (emplace path).
  {
    metl::variant<int, Tracked> v{Tracked{7}};
    v = 99;  // different alternative → destroy Tracked, activate int
    CHECK_EQ(v.index(), 0u);
    CHECK_EQ(metl::get<int>(v), 99);
  }

  // (4) Switching alternatives from a subobject of the active alternative.
  // The old path emplace()d directly, which
  // destroyed the `holder` before reading `holder::inner`. `poisoned` writes
  // -1 into itself on destruction, so the stale read shows up as a wrong value
  // even without a sanitizer.
  {
    struct poisoned {
      int value;
      explicit poisoned(int v) noexcept : value(v) {}
      poisoned(const poisoned& o) noexcept : value(o.value) {}
      poisoned(poisoned&& o) noexcept : value(o.value) {}
      poisoned& operator=(const poisoned&) noexcept = default;
      poisoned& operator=(poisoned&&) noexcept = default;
      // Volatile: a plain store to a dying object is a dead store GCC removes at
      // -O2, and a use-after-destroy would then read the old value and pass.
      ~poisoned() { *static_cast<volatile int*>(&value) = -1; }
    };
    struct holder {
      poisoned inner;
      int tag;
    };
    metl::variant<holder, poisoned> v{holder{poisoned{77}, 1}};
    v = metl::get<holder>(v).inner;
    CHECK_EQ(v.index(), 1u);
    CHECK_EQ(metl::get<poisoned>(v).value, 77);
  }

  return metl_test::exit_code();
}
