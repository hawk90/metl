// handles_and_pools.cpp
//
// Two ways to hand out fixed-capacity storage, and the reason METL prefers the
// second one.
//
//   object_pool<T, N>  gives you a T*.       Simple, and what you reach for first.
//   handle_pool<T, N>  gives you a 4-byte handle {index, generation}.
//
// They look interchangeable until something is freed. A pointer into a recycled
// slot is indistinguishable from a live one -- same address, same type, and the
// pool will even tell you it owns it -- so a stale pointer quietly reads whatever
// took its place. A handle carries a generation counter, so the pool can *detect*
// the staleness and hand back nullptr instead.
//
// This example makes that difference happen, on purpose, and checks it. See
// docs/SCOPE.md §7 for the design argument; this is the same argument you can run.
//
// Nothing here allocates: both pools are inline storage.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <metl/handle_pool.hpp>
#include <metl/object_pool.hpp>

namespace {

// A modem connection, the sort of thing a fixed-capacity table holds.
struct connection {
  std::uint16_t port;
  std::uint32_t bytes_sent;
};

int demo_object_pool() {
  metl::object_pool<connection, 4> pool;

  // try_emplace returns nullptr when full; emplace asserts instead. Capacity is
  // fixed at 4, so the fifth is refused rather than growing.
  connection* first = pool.try_emplace(connection{8080, 0});
  connection* second = pool.try_emplace(connection{9000, 0});
  if (first == nullptr || second == nullptr) {
    return 1;
  }
  if (pool.size() != 2 || pool.available() != 2) {
    return 2;
  }

  first->bytes_sent = 128;
  if (first->port != 8080 || first->bytes_sent != 128) {
    return 3;
  }

  // Fill it, then watch the fifth be refused.
  if (pool.try_emplace(connection{1, 0}) == nullptr) {
    return 4;
  }
  if (pool.try_emplace(connection{2, 0}) == nullptr) {
    return 5;
  }
  if (!pool.full() || pool.try_emplace(connection{3, 0}) != nullptr) {
    return 6;  // full: must refuse, never grow
  }

  // destroy() answers "was that a live slot of mine" -- a plain bool question,
  // not a failure report, which is why it keeps its plain name.
  if (!pool.destroy(second)) {
    return 7;
  }
  if (pool.destroy(second)) {
    return 8;  // already gone
  }

  pool.clear();
  return pool.empty() ? 0 : 9;
}

int demo_handle_pool() {
  metl::handle_pool<connection, 4> pool;

  const auto session = pool.try_emplace(connection{8080, 0});
  if (!session.valid()) {
    return 10;
  }

  // A handle is not a pointer: resolve it when you need the object, and check.
  connection* live = pool.get(session);
  if (live == nullptr || live->port != 8080) {
    return 11;
  }
  live->bytes_sent = 512;

  // The handle is 4 bytes and trivially copyable, so it is cheap to store in a
  // table, send in a message, or put in an atomic (see metl/atomic_handle.hpp).
  static_assert(sizeof(session) == 4, "a versioned_handle is index + generation");

  if (!pool.contains(session) || pool.size() != 1) {
    return 12;
  }
  if (!pool.destroy(session)) {
    return 13;
  }

  // THE POINT. The handle still exists in our hands, and resolving it now fails
  // cleanly rather than returning something plausible.
  if (pool.contains(session)) {
    return 14;
  }
  if (pool.get(session) != nullptr) {
    return 15;
  }
  if (pool.destroy(session)) {
    return 16;  // double free is detected, not silently repeated
  }
  return 0;
}

/// Runs both pools through the same "free it, then reuse the slot" sequence and
/// reports what a stale reference sees afterwards. No undefined behaviour: the
/// recycled slot holds a live object in both cases -- that is exactly the problem.
int demo_stale_reference() {
  // --- pointers ---
  metl::object_pool<connection, 2> pointers;
  connection* stale_pointer = pointers.try_emplace(connection{8080, 0});
  if (stale_pointer == nullptr) {
    return 20;
  }
  if (!pointers.destroy(stale_pointer)) {
    return 21;
  }
  connection* reused = pointers.try_emplace(connection{9999, 0});
  if (reused == nullptr) {
    return 22;
  }

  // The freed slot came back, so the old pointer now aliases a DIFFERENT
  // connection -- and nothing in the API can tell you that.
  const bool pointer_is_indistinguishable =
      (stale_pointer == reused) && pointers.contains(stale_pointer) && stale_pointer->port == 9999;
  if (!pointer_is_indistinguishable) {
    return 23;  // if this ever fails, the hazard below has changed shape
  }

  // --- handles ---
  metl::handle_pool<connection, 2> handles;
  const auto stale_handle = handles.try_emplace(connection{8080, 0});
  if (!stale_handle.valid()) {
    return 24;
  }
  if (!handles.destroy(stale_handle)) {
    return 25;
  }
  const auto fresh_handle = handles.try_emplace(connection{9999, 0});
  if (!fresh_handle.valid()) {
    return 26;
  }

  // Same slot, but the generation moved on, so the old handle no longer resolves.
  if (stale_handle.index() != fresh_handle.index()) {
    return 27;  // the slot really was reused -- otherwise this proves nothing
  }
  if (stale_handle.generation() == fresh_handle.generation()) {
    return 28;
  }
  if (handles.get(stale_handle) != nullptr) {
    return 29;  // THE difference: detected, not plausible
  }
  if (handles.get(fresh_handle) == nullptr || handles.get(fresh_handle)->port != 9999) {
    return 30;
  }

  std::printf("  stale pointer -> same address, pool says it owns it, reads port %u (WRONG)\n",
              static_cast<unsigned>(stale_pointer->port));
  std::printf("  stale handle  -> same slot %u, generation %u vs %u, get() returns nullptr\n",
              static_cast<unsigned>(stale_handle.index()),
              static_cast<unsigned>(stale_handle.generation()),
              static_cast<unsigned>(fresh_handle.generation()));
  return 0;
}

}  // namespace

int main() {
  if (const int rc = demo_object_pool(); rc != 0) {
    std::printf("object_pool demo failed: %d\n", rc);
    return rc;
  }
  if (const int rc = demo_handle_pool(); rc != 0) {
    std::printf("handle_pool demo failed: %d\n", rc);
    return rc;
  }
  std::printf("handles_and_pools: after freeing a slot and reusing it,\n");
  if (const int rc = demo_stale_reference(); rc != 0) {
    std::printf("stale-reference demo failed: %d\n", rc);
    return rc;
  }
  std::printf("Use object_pool when the pointer never outlives the object.\n");
  std::printf("Use handle_pool when it might -- a stale handle fails loudly.\n");
  return 0;
}
