// Regression test for object_pool::contains / index_of (AUDIT E.2): the
// membership test must not use relational operators (`<`, `>=`) on a
// caller-supplied pointer that may not point into the pool — that is UB for
// unrelated pointers. The fix compares integer addresses instead. This test
// feeds contains() foreign, null, and same-pool pointers.
#include "metl_check.hpp"

#include <metl/object_pool.hpp>

int main() {
  metl::object_pool<int, 4> pool;
  int* a = pool.emplace(10);
  int* b = pool.emplace(20);
  CHECK(a != nullptr);
  CHECK(b != nullptr);

  // Pointers actually in the pool are members.
  CHECK(pool.contains(a));
  CHECK(pool.contains(b));

  // A null pointer is never a member (and must not be compared relationally).
  CHECK(!pool.contains(nullptr));

  // A pointer to an unrelated stack object is not a member.
  int stack_object = 123;
  CHECK(!pool.contains(&stack_object));

  // A pointer owned by a DIFFERENT pool is not a member of this one (exercises
  // the unrelated-pointer path that previously invoked UB).
  metl::object_pool<int, 4> other;
  int* foreign = other.emplace(99);
  CHECK(foreign != nullptr);
  CHECK(!pool.contains(foreign));
  CHECK(other.contains(foreign));

  return metl_test::exit_code();
}
