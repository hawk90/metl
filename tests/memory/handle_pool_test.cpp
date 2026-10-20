#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/handle_pool.hpp>

namespace {

struct tracker {
  static int constructions;
  static int destructions;

  tracker() : value(0) { ++constructions; }
  explicit tracker(int input) : value(input) { ++constructions; }
  ~tracker() { ++destructions; }

  int value;
};

int tracker::constructions = 0;
int tracker::destructions = 0;

void reset_tracker() {
  tracker::constructions = 0;
  tracker::destructions = 0;
}

}  // namespace

int main() {
  // --- Basic lifecycle -------------------------------------------------------
  {
    reset_tracker();
    metl::handle_pool<tracker, 2> pool;
    CHECK(pool.empty());
    CHECK_EQ(pool.capacity(), std::size_t{2});
    CHECK_EQ(pool.available(), std::size_t{2});

    const auto first = pool.try_emplace(10);
    const auto second = pool.try_emplace(20);
    CHECK(first.valid());
    CHECK(second.valid());
    CHECK(first != second);
    CHECK_EQ(pool.size(), std::size_t{2});
    CHECK(pool.full());
    CHECK_EQ(tracker::constructions, 2);

    const tracker* first_slot = pool.get(first);
    if (CHECK(first_slot != nullptr)) {
      CHECK_EQ(first_slot->value, 10);
    }
    const tracker* second_slot = pool.get(second);
    if (CHECK(second_slot != nullptr)) {
      CHECK_EQ(second_slot->value, 20);
    }
    CHECK(pool.contains(first));

    // A full pool hands back a null handle rather than allocating.
    const auto overflow = pool.try_emplace(30);
    CHECK(!overflow.valid());
    CHECK_EQ(pool.get(overflow), static_cast<tracker*>(nullptr));
    CHECK_EQ(tracker::constructions, 2);

    CHECK(pool.destroy(first));
    CHECK_EQ(tracker::destructions, 1);
    CHECK_EQ(pool.size(), std::size_t{1});
  }

  // --- Use-after-free is detected, not undefined -----------------------------
  {
    reset_tracker();
    metl::handle_pool<tracker, 2> pool;

    const auto handle = pool.try_emplace(1);
    CHECK(pool.get(handle) != nullptr);

    CHECK(pool.destroy(handle));
    // The handle itself is still a well-formed non-null value; what changed is
    // that it no longer matches its slot.
    CHECK(handle.valid());
    CHECK_EQ(pool.get(handle), static_cast<tracker*>(nullptr));
    CHECK(!pool.contains(handle));

    // Double destroy is reported, not undefined.
    CHECK(!pool.destroy(handle));
    CHECK_EQ(tracker::destructions, 1);
  }

  // --- The ABA case: a recycled slot does not resurrect an old handle --------
  {
    reset_tracker();
    metl::handle_pool<tracker, 1> pool;

    const auto old_handle = pool.try_emplace(1);
    const auto old_index = old_handle.index();
    CHECK(pool.destroy(old_handle));

    // Capacity 1, so this necessarily reuses the same slot -- the exact
    // situation where a raw pointer or an index alone would silently alias.
    const auto new_handle = pool.try_emplace(2);
    CHECK_EQ(new_handle.index(), old_index);
    CHECK(new_handle != old_handle);
    CHECK(new_handle.generation() != old_handle.generation());

    CHECK_EQ(pool.get(old_handle), static_cast<tracker*>(nullptr));
    const tracker* reused = pool.get(new_handle);
    if (CHECK(reused != nullptr)) {
      CHECK_EQ(reused->value, 2);
    }
  }

  // --- clear() destroys everything and stales every handle -------------------
  {
    reset_tracker();
    metl::handle_pool<tracker, 4> pool;

    const auto a = pool.try_emplace(1);
    const auto b = pool.try_emplace(2);
    const auto c = pool.try_emplace(3);
    CHECK_EQ(pool.size(), std::size_t{3});

    pool.clear();
    CHECK(pool.empty());
    CHECK_EQ(pool.available(), std::size_t{4});
    CHECK_EQ(tracker::destructions, 3);
    CHECK_EQ(pool.get(a), static_cast<tracker*>(nullptr));
    CHECK_EQ(pool.get(b), static_cast<tracker*>(nullptr));
    CHECK_EQ(pool.get(c), static_cast<tracker*>(nullptr));

    // The pool is fully usable afterwards.
    const auto d = pool.try_emplace(4);
    CHECK(pool.get(d) != nullptr);
  }

  // --- Destructor destroys live objects --------------------------------------
  {
    reset_tracker();
    {
      metl::handle_pool<tracker, 4> pool;
      (void)pool.try_emplace(1);
      (void)pool.try_emplace(2);
    }
    CHECK_EQ(tracker::destructions, 2);
  }

  // --- Slots are independent -------------------------------------------------
  {
    metl::handle_pool<int, 4> pool;
    metl::handle_pool<int, 4>::handle_type handles[4];
    for (int i = 0; i < 4; ++i) {
      handles[i] = pool.try_emplace(i * 100);
    }
    // Free the middle two, then refill: the survivors must be untouched.
    CHECK(pool.destroy(handles[1]));
    CHECK(pool.destroy(handles[2]));
    const auto refilled_a = pool.try_emplace(555);
    const auto refilled_b = pool.try_emplace(666);
    CHECK_DEREF_EQ(pool.get(handles[0]), 0);
    CHECK_DEREF_EQ(pool.get(handles[3]), 300);
    CHECK_DEREF_EQ(pool.get(refilled_a), 555);
    CHECK_DEREF_EQ(pool.get(refilled_b), 666);
    CHECK(pool.full());
  }

  // --- Zero capacity is a valid degenerate pool ------------------------------
  // Every other fixed-capacity METL container accepts Capacity == 0, so this one
  // does too rather than rejecting it at compile time.
  {
    metl::handle_pool<int, 0> pool;
    CHECK(pool.empty());
    CHECK(pool.full());
    CHECK_EQ(pool.capacity(), std::size_t{0});
    CHECK_EQ(pool.available(), std::size_t{0});

    const auto handle = pool.try_emplace(1);
    CHECK(!handle.valid());
    CHECK_EQ(pool.get(handle), static_cast<int*>(nullptr));
    CHECK(!pool.contains(handle));
    CHECK(!pool.destroy(handle));
    pool.clear();
  }

  // --- The const get() rejects what the non-const one rejects -----------------
  {
    metl::handle_pool<int, 2> pool;
    const auto& view = pool;
    const auto live = pool.try_emplace(7);
    const auto stale = pool.try_emplace(8);
    CHECK(pool.destroy(stale));
    CHECK_DEREF_EQ(view.get(live), 7);
    CHECK_EQ(view.get(stale), static_cast<const int*>(nullptr));
    CHECK_EQ(view.get(metl::handle_pool<int, 2>::handle_type{}), static_cast<const int*>(nullptr));
  }

  // --- A forged handle to a never-used slot does not resolve -------------------
  // Every slot starts at generation 1, so `{unused index, 1}` matches the
  // slot's generation; only the slot being inactive rejects it.
  {
    metl::handle_pool<int, 4> pool;
    const auto& view = pool;
    const auto issued = pool.try_emplace(1);
    CHECK_EQ(issued.index(), 0u);
    const metl::handle_pool<int, 4>::handle_type forged{2, 1};
    CHECK_EQ(pool.generation_of(2), 1u);
    CHECK(!pool.contains(forged));
    CHECK_EQ(pool.get(forged), static_cast<int*>(nullptr));
    CHECK_EQ(view.get(forged), static_cast<const int*>(nullptr));
    CHECK(!pool.destroy(forged));
    CHECK_EQ(pool.size(), std::size_t{1});
    // Out of range at a matching generation is rejected too.
    const metl::handle_pool<int, 4>::handle_type out_of_range{4, 1};
    CHECK(!pool.contains(out_of_range));
  }

  // --- A forged handle to a freed slot at its current generation does not ------
  // resolve. The generation matches, so only the slot being inactive rejects it.
  {
    metl::handle_pool<int, 4> pool;
    const auto handle = pool.try_emplace(1);
    CHECK(pool.destroy(handle));
    const metl::handle_pool<int, 4>::handle_type forged{handle.index(), pool.generation_of(handle.index())};
    CHECK(forged.generation() != handle.generation());
    CHECK(!pool.contains(forged));
    CHECK_EQ(pool.get(forged), static_cast<int*>(nullptr));
    CHECK(!pool.destroy(forged));
    CHECK(pool.empty());
  }

  // --- Generation wraparound skips 0 -----------------------------------------
  // With an 8-bit counter the whole cycle is short enough to walk exhaustively.
  // Generation 0 is the null marker, so a slot must never land on it -- if it
  // did, every stale handle to that slot would compare equal to a null handle.
  {
    metl::handle_pool<int, 1, std::uint8_t> pool;
    for (int cycle = 0; cycle < 600; ++cycle) {
      const auto handle = pool.try_emplace(cycle);
      CHECK(handle.valid());
      CHECK(handle.generation() != 0);
      CHECK_DEREF_EQ(pool.get(handle), cycle);
      CHECK(pool.destroy(handle));
    }
    CHECK(pool.generation_of(0) != 0);
  }

  // --- A new pool hands out 0, 1, 2, ...; a freed slot is reused first --------
  {
    metl::handle_pool<int, 4> pool;
    for (std::size_t i = 0; i < 4; ++i) {
      const auto handle = pool.try_emplace(static_cast<int>(i));
      CHECK_EQ(static_cast<std::size_t>(handle.index()), i);
      CHECK_EQ(static_cast<unsigned>(handle.generation()), 1u);
    }
    CHECK(!pool.try_emplace(9).valid());
    CHECK(pool.destroy(metl::handle_pool<int, 4>::handle_type{2, 1}));
    CHECK(pool.destroy(metl::handle_pool<int, 4>::handle_type{0, 1}));
    const auto first = pool.try_emplace(10);
    const auto second = pool.try_emplace(11);
    CHECK_EQ(static_cast<unsigned>(first.index()), 0u);
    CHECK_EQ(static_cast<unsigned>(first.generation()), 2u);
    CHECK_EQ(static_cast<unsigned>(second.index()), 2u);
    CHECK(!pool.try_emplace(12).valid());
  }

  // --- clear() keeps every generation: no handle from before it resolves ------
  // Slots freed by clear() are handed out again at their next generation, so a
  // handle taken before clear() never names the object built after it.
  {
    metl::handle_pool<int, 4> pool;
    const auto before0 = pool.try_emplace(1);
    const auto before1 = pool.try_emplace(2);
    pool.clear();
    CHECK(pool.empty());
    const auto after0 = pool.try_emplace(3);
    const auto after1 = pool.try_emplace(4);
    CHECK(!pool.contains(before0));
    CHECK(!pool.contains(before1));
    CHECK(pool.contains(after0));
    CHECK(pool.contains(after1));
    CHECK_EQ(static_cast<unsigned>(after0.generation()), 2u);
    CHECK_EQ(static_cast<unsigned>(after1.generation()), 2u);
  }

  // --- The largest capacity fills exactly and reuses its last slot ------------
  // Every free-list value is at most max_capacity, the edge of index_type.
  {
    using big_pool = metl::handle_pool<unsigned char, metl::handle_pool<unsigned char, 1>::max_capacity>;
    static big_pool pool;
    for (std::size_t i = 0; i < big_pool::max_capacity; ++i) {
      const auto handle = pool.try_emplace(static_cast<unsigned char>(i));
      if (static_cast<std::size_t>(handle.index()) != i) {
        CHECK_EQ(static_cast<std::size_t>(handle.index()), i);
        break;
      }
    }
    CHECK(pool.full());
    CHECK(!pool.try_emplace(static_cast<unsigned char>(0)).valid());
    const big_pool::handle_type last{static_cast<big_pool::index_type>(big_pool::max_capacity - 1), 1};
    CHECK(pool.destroy(last));
    const auto again = pool.try_emplace(static_cast<unsigned char>(7));
    CHECK_EQ(static_cast<std::size_t>(again.index()), big_pool::max_capacity - 1);
    CHECK(pool.full());
    pool.clear();
    CHECK(pool.empty());
  }

  return metl_test::exit_code();
}
