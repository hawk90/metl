// Regression test for the arena_allocator::try_emplace exception-safety fix
// (AUDIT E.2): the destructor record must be registered only AFTER the object is
// constructed. The old code committed the record first, so a throwing
// constructor left a record pointing at unconstructed storage and a later
// rewind/reset ran ~T() on raw memory (UB). Only reachable when exceptions are
// enabled; a no-op pass under METL_NO_EXCEPTIONS.
#include "metl_check.hpp"

#include <metl/arena_allocator.hpp>
#include <metl/config.hpp>

namespace {

struct Boom {
  inline static int ctors = 0;
  inline static int dtors = 0;
  static void reset_counts() { ctors = dtors = 0; }

  int value;
  explicit Boom(bool should_throw, int v = 0) : value(v) {
    if (should_throw) {
      throw 1;
    }
    ++ctors;
  }
  ~Boom() { ++dtors; }
};

}  // namespace

int main() {
  // Happy path: a successful emplace registers a destructor that reset() runs.
  {
    Boom::reset_counts();
    metl::arena_allocator<256> arena;
    Boom* p = arena.try_emplace<Boom>(false, 5);
    CHECK(p != nullptr);
    CHECK_EQ(Boom::ctors, 1);
    arena.reset();
    CHECK_EQ(Boom::dtors, 1);  // destructor ran exactly once
  }

#if !METL_NO_EXCEPTIONS
  // Throwing constructor: no object is constructed, and reset() must NOT run a
  // destructor over the never-constructed storage.
  {
    Boom::reset_counts();
    metl::arena_allocator<256> arena;
    bool threw = false;
    try {
      (void)arena.try_emplace<Boom>(true);
    } catch (...) {
      threw = true;
    }
    CHECK(threw);
    CHECK_EQ(Boom::ctors, 0);

    // The arena is still usable after the failed allocation.
    Boom* ok = arena.try_emplace<Boom>(false, 9);
    CHECK(ok != nullptr);
    CHECK_EQ(Boom::ctors, 1);

    arena.reset();
    // Exactly one destructor: for the successful object only. The failed
    // allocation left NO destroy record, so ~Boom() never ran on raw storage.
    CHECK_EQ(Boom::dtors, 1);
  }
#endif

  return metl_test::exit_code();
}
