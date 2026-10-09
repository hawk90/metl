// The atomic reference count, apart from tests/vocab/intrusive_ptr_test.cpp so
// that the rest of intrusive_ptr runs on ARMv6-M. There this file must FAIL to
// build: refcount_kind::atomic needs a lock-free read-modify-write that
// Cortex-M0 lacks, and the qemu-conformance M0 row lists it as an expected
// build failure, which proves the capability static_assert fires.
#include <cstddef>
#include <new>

#include <metl/intrusive_ptr.hpp>

namespace {

template <typename T>
struct storage_for {
  alignas(T) unsigned char buf[sizeof(T)];
  template <typename... Args>
  T* construct(Args&&... args) {
    return ::new (static_cast<void*>(buf)) T(static_cast<Args&&>(args)...);
  }
};

struct atomic_obj final : metl::intrusive_ref_counter<atomic_obj, metl::refcount_kind::atomic> {
  explicit atomic_obj(int v) : value(v) {}
  ~atomic_obj() { ++destruction_count; }
  int value;
  static int destruction_count;
};

int atomic_obj::destruction_count = 0;

}  // namespace

// CRTP atomic refcounter test.
static int crtp_atomic_test() {
  atomic_obj::destruction_count = 0;

  storage_for<atomic_obj> store;
  auto* raw = store.construct(99);

  {
    metl::intrusive_ptr<atomic_obj> p(raw, metl::retain_ref);
    if (!p || p->value != 99 || raw->use_count() != 1) {
      return 200;
    }
    metl::intrusive_ptr<atomic_obj> q(p);
    if (raw->use_count() != 2) {
      return 201;
    }
    q.reset();
    if (raw->use_count() != 1) {
      return 202;
    }
  }

  if (atomic_obj::destruction_count != 1) {
    return 203;
  }
  return 0;
}

int main() {
  return crtp_atomic_test();
}
