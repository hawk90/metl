#include "metl_check.hpp"

#include <cstdint>

#include <metl/atomic_handle.hpp>
#include <metl/handle_pool.hpp>
#include <metl/versioned_handle.hpp>

namespace {

struct tag {};
using handle = metl::versioned_handle<tag>;

// The host must be able to instantiate atomic_handle at all; if this trait were
// false here, every test below would be a compile error rather than a failure.
static_assert(metl::has_lock_free_handle_atomic_v<handle>, "host is expected to have a lock-free 32-bit CAS");
static_assert(metl::atomic_handle<handle>::is_always_lock_free, "instantiation implies lock-free");

// The point of the whole design: the atomic cell is one machine word, so the
// compare-exchange below is a plain single-word CAS -- no double-width CAS, no
// pointer-bit stuffing.
static_assert(sizeof(metl::atomic_handle<handle>) == sizeof(handle),
              "atomic_handle must not be larger than the handle it holds");

}  // namespace

int main() {
  // --- load / store / exchange ------------------------------------------------
  {
    metl::atomic_handle<handle> cell;
    CHECK(!cell.load().valid());

    cell.store(handle{7, 3});
    CHECK_EQ(cell.load(), handle(7, 3));

    const handle previous = cell.exchange(handle{9, 4});
    CHECK_EQ(previous, handle(7, 3));
    CHECK_EQ(cell.load(), handle(9, 4));
  }

  // --- compare_exchange succeeds when the observed value matches --------------
  {
    metl::atomic_handle<handle> cell{handle{1, 1}};
    handle expected{1, 1};
    CHECK(cell.compare_exchange_strong(expected, handle{2, 1}));
    CHECK_EQ(cell.load(), handle(2, 1));
  }

  // --- compare_exchange fails and reports what it saw -------------------------
  {
    metl::atomic_handle<handle> cell{handle{5, 2}};
    handle expected{5, 1};  // right slot, wrong generation
    CHECK(!cell.compare_exchange_strong(expected, handle{6, 2}));
    // On failure `expected` is updated to the observed value, as with
    // std::atomic::compare_exchange_*.
    CHECK_EQ(expected, handle(5, 2));
    CHECK_EQ(cell.load(), handle(5, 2));
  }

  // --- ABA: a stale handle cannot win the exchange ----------------------------
  // This is the reason the generation lives in the same word as the index. With
  // a bare index (or a bare pointer), the CAS below would SUCCEED even though
  // the slot was freed and handed out again in between -- the classic ABA bug.
  {
    metl::handle_pool<int, 1> pool;

    const auto first = pool.try_emplace(1);
    metl::atomic_handle<metl::handle_pool<int, 1>::handle_type> head{first};

    CHECK(pool.destroy(first));
    const auto second = pool.try_emplace(2);  // same slot, new generation
    CHECK_EQ(second.index(), first.index());
    head.store(second);

    // A thread still holding `first` tries to publish based on it.
    auto stale = first;
    CHECK(!head.compare_exchange_strong(stale, metl::handle_pool<int, 1>::handle_type{}));
    CHECK_EQ(stale, second);  // it observes the current value instead
    CHECK_EQ(head.load(), second);

    // The same CAS with the current handle does succeed.
    auto fresh = second;
    CHECK(head.compare_exchange_strong(fresh, metl::handle_pool<int, 1>::handle_type{}));
    CHECK(!head.load().valid());
  }

  // --- weak form drives a retry loop ------------------------------------------
  {
    metl::atomic_handle<handle> cell{handle{0, 1}};
    handle observed = cell.load();
    while (
        !cell.compare_exchange_weak(observed, handle{static_cast<std::uint16_t>(observed.index() + 1), 1})) {
      // observed was refreshed by the failed exchange; retry.
    }
    CHECK_EQ(cell.load(), handle(1, 1));
  }

  return metl_test::exit_code();
}
