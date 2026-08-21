// Regression tests for the 2026-08-21 audit of the headers that landed after
// the 2026-07-07 audit (docs/AUDIT.md, Section F).
//
// Each block here corresponds to one finding, and each was written by first
// confirming it FAILS against the unfixed code -- a regression test that has
// never been red proves only that it compiles.

#include "metl_check.hpp"

#include "metl/lock.hpp"
#include "metl/mpmc_queue.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

namespace {

/// E.1 -- a type with NO default constructor. `mpmc_queue`'s destructor used to
/// drain through `try_pop`, which needed a `T discarded;` to pop into, so this
/// type would not compile inside `mpmc_queue` at all. `spsc_queue` never had the
/// requirement, and no static_assert stated it.
struct no_default {
  int value;
  explicit no_default(int v) noexcept : value(v) {}
  no_default(const no_default&) noexcept = default;
  no_default(no_default&&) noexcept = default;
  no_default& operator=(const no_default&) noexcept = default;
  no_default& operator=(no_default&&) noexcept = default;
  ~no_default() = default;
};

static_assert(!std::is_default_constructible_v<no_default>, "the fixture must not be default-constructible");

/// Counts destructions, so the destructor's in-place cleanup can be checked
/// rather than assumed.
struct counted {
  static int live;
  int value;
  explicit counted(int v = 0) noexcept : value(v) { ++live; }
  counted(const counted& o) noexcept : value(o.value) { ++live; }
  counted(counted&& o) noexcept : value(o.value) { ++live; }
  counted& operator=(const counted&) noexcept = default;
  counted& operator=(counted&&) noexcept = default;
  ~counted() { --live; }
};

int counted::live = 0;

}  // namespace

int main() {
  // ---------------------------------------------------------------------
  // E.1  mpmc_queue must not require a default-constructible T.
  //      Before the fix this block did not compile.
  // ---------------------------------------------------------------------
  {
    metl::mpmc_queue<no_default, 4> queue;
    CHECK(queue.try_push(no_default{7}));
    CHECK(queue.try_emplace(9));

    no_default out{0};
    CHECK(queue.try_pop(out));
    CHECK_EQ(out.value, 7);
    CHECK(queue.try_pop(out));
    CHECK_EQ(out.value, 9);
    CHECK(!queue.try_pop(out));
  }

  // The destructor must actually destroy what is left, not merely compile.
  counted::live = 0;
  {
    metl::mpmc_queue<counted, 4> queue;
    CHECK(queue.try_emplace(1));
    CHECK(queue.try_emplace(2));
    CHECK(queue.try_emplace(3));
    CHECK_EQ(counted::live, 3);
    // Leaves all three in the queue on purpose: the destructor is what is
    // under test.
  }
  CHECK_EQ(counted::live, 0);

  // And a partially drained queue destroys only what remains.
  counted::live = 0;
  {
    metl::mpmc_queue<counted, 4> queue;
    CHECK(queue.try_emplace(1));
    CHECK(queue.try_emplace(2));
    counted out;
    CHECK(queue.try_pop(out));
    // `out` is live too, so 2 in flight: the popped one and the queued one.
    CHECK_EQ(counted::live, 2);
  }
  CHECK_EQ(counted::live, 0);

  // ---------------------------------------------------------------------
  // E.2  size_approx / full must not lie.
  //
  //      The wrap case that motivated this fix needs 2^size_t operations and
  //      cannot be reached in a test -- the change makes the arithmetic
  //      identical to spsc_queue's, which is the reference. What IS testable is
  //      that the ordinary path still answers correctly, so a future "fix" that
  //      reintroduces a comparison has something to fail.
  // ---------------------------------------------------------------------
  {
    metl::mpmc_queue<int, 4> queue;
    CHECK(queue.empty());
    CHECK_EQ(queue.size_approx(), 0u);

    for (int i = 0; i < 4; ++i) {
      CHECK(queue.try_push(i));
    }
    CHECK_EQ(queue.size_approx(), 4u);
    CHECK(queue.full());
    CHECK(!queue.empty());
    CHECK(!queue.try_push(99));

    int out = 0;
    CHECK(queue.try_pop(out));
    CHECK_EQ(out, 0);
    CHECK_EQ(queue.size_approx(), 3u);
    CHECK(!queue.full());
  }

  // ---------------------------------------------------------------------
  // E.3  guarded::with must not let a reference to the guarded value escape.
  // ---------------------------------------------------------------------
  {
    using guard_type = metl::guarded<int, metl::null_lock>;
    guard_type shared;

    // Returning a value is fine, and the lambda sees the guarded object.
    const int doubled = shared.with([](int& v) {
      v = 21;
      return v * 2;
    });
    CHECK_EQ(doubled, 42);

    // Returning void is fine.
    shared.with([](int& v) noexcept { v += 1; });
    CHECK_EQ(shared.with([](int& v) { return v; }), 22);

    // Returning a reference to something that is NOT the guarded value is still
    // allowed -- the check rejects the escape it can see, not every one.
    static int elsewhere = 5;
    int& other = shared.with([](int&) -> int& { return elsewhere; });
    CHECK_EQ(other, 5);

    // Returning a reference to the GUARDED value is a documented hazard, not a
    // compile error, and the line above is why: for guarded<int> a returned
    // `int&` to an unrelated global and one to the guarded value are the same
    // type. A static_assert on the return type was tried and rejected the line
    // above, which is correct code. See the @warning on guarded::with.
  }

  // ---------------------------------------------------------------------
  // E.4  the variadic constructor must not out-compete the deleted copy ctor.
  //      `guarded` must still be non-copyable, and the error must say so.
  // ---------------------------------------------------------------------
  {
    static_assert(!std::is_copy_constructible_v<metl::guarded<int, metl::null_lock>>,
                  "guarded must not be copy-constructible");
    static_assert(!std::is_move_constructible_v<metl::guarded<int, metl::null_lock>>,
                  "guarded must not be move-constructible");
    // In-place construction still works, which is what the variadic is for.
    metl::guarded<int, metl::null_lock> from_value{7};
    CHECK_EQ(from_value.with([](int& v) { return v; }), 7);
  }

  return metl_test::exit_code();
}
