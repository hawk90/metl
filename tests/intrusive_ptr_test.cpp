#include <cstddef>
#include <functional>
#include <new>
#include <type_traits>

#include <metl/intrusive_ptr.hpp>

namespace {

// Aligned storage helpers (avoid the heap entirely).
template <typename T>
struct storage_for {
  alignas(T) unsigned char buf[sizeof(T)];
  template <typename... Args>
  T* construct(Args&&... args) {
    return ::new (static_cast<void*>(buf)) T(static_cast<Args&&>(args)...);
  }
};

// ---------------------------------------------------------------------------
// Legacy ADL-hook based node (existing compatibility)
// ---------------------------------------------------------------------------

struct node {
  explicit node(int input) : value(input), ref_count(0) {}

  int value;
  int ref_count;
  static int destructions;
};

int node::destructions = 0;

void intrusive_ptr_add_ref(node* object) {
  ++object->ref_count;
}

void intrusive_ptr_release(node* object) {
  --object->ref_count;
  if (object->ref_count == 0) {
    ++node::destructions;
    // No heap: caller owns the storage_for<node>. We only track the
    // destruction count here; the underlying object is destroyed by
    // the storage owner (or its automatic lifetime).
  }
}

// ---------------------------------------------------------------------------
// CRTP refcounted types backed by aligned static storage (no heap).
// ---------------------------------------------------------------------------

struct non_atomic_obj : metl::intrusive_ref_counter<non_atomic_obj, metl::refcount_kind::non_atomic> {
  explicit non_atomic_obj(int v) : value(v) {}
  ~non_atomic_obj() { ++destruction_count; }
  int value;
  static int destruction_count;
};

int non_atomic_obj::destruction_count = 0;

struct atomic_obj : metl::intrusive_ref_counter<atomic_obj, metl::refcount_kind::atomic> {
  explicit atomic_obj(int v) : value(v) {}
  ~atomic_obj() { ++destruction_count; }
  int value;
  static int destruction_count;
};

int atomic_obj::destruction_count = 0;

// ---------------------------------------------------------------------------
// Inheritance test for converting constructor
// ---------------------------------------------------------------------------

struct base : metl::intrusive_ref_counter<base, metl::refcount_kind::non_atomic> {
  explicit base(int v) : value(v) {}
  virtual ~base() = default;
  int value;
};

struct derived : base {
  explicit derived(int v, int e) : base(v), extra(e) {}
  int extra;
};

}  // namespace

// Legacy test (kept verbatim for compatibility).
static int legacy_test() {
  node::destructions = 0;

  static storage_for<node> node_storage;
  node* raw = node_storage.construct(7);
  metl::intrusive_ptr<node> first(raw, metl::retain_ref);
  if (!first || first->value != 7 || raw->ref_count != 1) {
    return 1;
  }

  {
    metl::intrusive_ptr<node> second(first);
    if (raw->ref_count != 2 || second->value != 7) {
      return 2;
    }

    metl::intrusive_ptr<node> third(static_cast<metl::intrusive_ptr<node>&&>(second));
    if (!third || second.get() != nullptr || raw->ref_count != 2) {
      return 3;
    }

    metl::intrusive_ptr<node> adopted(raw, metl::adopt_ref);
    intrusive_ptr_add_ref(raw);
    if (raw->ref_count != 3) {
      return 4;
    }

    adopted.reset();
    if (raw->ref_count != 2) {
      return 5;
    }
  }

  if (raw->ref_count != 1) {
    return 6;
  }

  first.reset();
  if (node::destructions != 1) {
    return 7;
  }

  return 0;
}

// CRTP non-atomic refcounter test.
static int crtp_non_atomic_test() {
  non_atomic_obj::destruction_count = 0;

  storage_for<non_atomic_obj> store;
  auto* raw = store.construct(42);

  {
    metl::intrusive_ptr<non_atomic_obj> p(raw, metl::retain_ref);
    if (!p || p->value != 42 || raw->use_count() != 1) {
      return 100;
    }

    {
      metl::intrusive_ptr<non_atomic_obj> q = p;
      if (raw->use_count() != 2) {
        return 101;
      }

      metl::intrusive_ptr<non_atomic_obj> r;
      r = q;
      if (raw->use_count() != 3) {
        return 102;
      }
    }

    if (raw->use_count() != 1) {
      return 103;
    }

    if (non_atomic_obj::destruction_count != 0) {
      return 104;
    }
  }

  if (non_atomic_obj::destruction_count != 1) {
    return 105;
  }
  return 0;
}

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

// Comparison operators test.
static int comparison_test() {
  storage_for<non_atomic_obj> a_store;
  storage_for<non_atomic_obj> b_store;
  auto* a_raw = a_store.construct(1);
  auto* b_raw = b_store.construct(2);

  metl::intrusive_ptr<non_atomic_obj> a(a_raw, metl::retain_ref);
  metl::intrusive_ptr<non_atomic_obj> b(b_raw, metl::retain_ref);
  metl::intrusive_ptr<non_atomic_obj> a2(a_raw, metl::retain_ref);
  metl::intrusive_ptr<non_atomic_obj> null_p;

  // Self equality.
  if (!(a == a2)) {
    return 300;
  }
  if (a != a2) {
    return 301;
  }
  // Distinct pointers.
  if (a == b) {
    return 302;
  }
  if (!(a != b)) {
    return 303;
  }

  // Ordering must be consistent with std::less<void>.
  bool a_lt_b = std::less<void*>{}(static_cast<void*>(a_raw), static_cast<void*>(b_raw));
  if ((a < b) != a_lt_b) {
    return 304;
  }
  if ((b > a) != a_lt_b) {
    return 305;
  }
  if (!(a <= a2) || !(a >= a2)) {
    return 306;
  }

  // Raw pointer comparisons.
  if (!(a == a_raw) || !(a_raw == a)) {
    return 307;
  }
  if (a != a_raw || a_raw != a) {
    return 308;
  }
  if (a == b_raw) {
    return 309;
  }

  // nullptr comparisons.
  if (!(null_p == nullptr) || !(nullptr == null_p)) {
    return 310;
  }
  if (null_p != nullptr || nullptr != null_p) {
    return 311;
  }
  if (a == nullptr || nullptr == a) {
    return 312;
  }
  if (!(a != nullptr) || !(nullptr != a)) {
    return 313;
  }
  if (!(null_p <= nullptr) || !(null_p >= nullptr)) {
    return 314;
  }

  // Release retained references so no dangling counters remain.
  a.reset();
  a2.reset();
  b.reset();
  return 0;
}

// std::hash test.
static int hash_test() {
  storage_for<non_atomic_obj> store;
  auto* raw = store.construct(7);

  metl::intrusive_ptr<non_atomic_obj> p(raw, metl::retain_ref);
  metl::intrusive_ptr<non_atomic_obj> q(raw, metl::retain_ref);
  metl::intrusive_ptr<non_atomic_obj> empty;

  std::hash<metl::intrusive_ptr<non_atomic_obj>> hasher;
  std::hash<non_atomic_obj*> raw_hasher;

  if (hasher(p) != raw_hasher(raw)) {
    return 400;
  }
  if (hasher(p) != hasher(q)) {
    return 401;
  }
  if (hasher(empty) != raw_hasher(nullptr)) {
    return 402;
  }

  p.reset();
  q.reset();
  return 0;
}

// Swap test (member and free function).
static int swap_test() {
  storage_for<non_atomic_obj> a_store;
  storage_for<non_atomic_obj> b_store;
  auto* a_raw = a_store.construct(1);
  auto* b_raw = b_store.construct(2);

  metl::intrusive_ptr<non_atomic_obj> a(a_raw, metl::retain_ref);
  metl::intrusive_ptr<non_atomic_obj> b(b_raw, metl::retain_ref);

  a.swap(b);
  if (a.get() != b_raw || b.get() != a_raw) {
    return 500;
  }
  if (a_raw->use_count() != 1 || b_raw->use_count() != 1) {
    return 501;
  }

  using std::swap;
  swap(a, b);
  if (a.get() != a_raw || b.get() != b_raw) {
    return 502;
  }

  metl::swap(a, b);
  if (a.get() != b_raw || b.get() != a_raw) {
    return 503;
  }

  a.reset();
  b.reset();
  return 0;
}

// Converting constructor test (derived -> base).
static int converting_ctor_test() {
  storage_for<derived> store;
  auto* d_raw = store.construct(11, 22);

  metl::intrusive_ptr<derived> d(d_raw, metl::retain_ref);
  if (d->use_count() != 1) {
    return 600;
  }

  // Converting copy.
  metl::intrusive_ptr<base> b(d);
  if (b.get() != static_cast<base*>(d_raw) || b->value != 11) {
    return 601;
  }
  if (d_raw->use_count() != 2) {
    return 602;
  }

  // Converting move.
  metl::intrusive_ptr<derived> d2(d_raw, metl::retain_ref);
  if (d_raw->use_count() != 3) {
    return 603;
  }
  metl::intrusive_ptr<base> b2(static_cast<metl::intrusive_ptr<derived>&&>(d2));
  if (d2.get() != nullptr || b2.get() != static_cast<base*>(d_raw)) {
    return 604;
  }
  if (d_raw->use_count() != 3) {
    return 605;  // moved: refcount unchanged
  }

  b.reset();
  b2.reset();
  d.reset();
  return 0;
}

// detach test (no decrement).
static int detach_test() {
  storage_for<non_atomic_obj> store;
  auto* raw = store.construct(5);

  metl::intrusive_ptr<non_atomic_obj> p(raw, metl::retain_ref);
  if (raw->use_count() != 1) {
    return 700;
  }
  auto* detached = p.detach();
  if (detached != raw || p.get() != nullptr) {
    return 701;
  }
  if (raw->use_count() != 1) {
    return 702;
  }
  // Re-adopt to clean up.
  metl::intrusive_ptr<non_atomic_obj> reclaim(detached, metl::adopt_ref);
  reclaim.reset();
  return 0;
}

// has_value / operator bool sanity.
static int bool_test() {
  metl::intrusive_ptr<non_atomic_obj> empty;
  if (empty || empty.has_value()) {
    return 800;
  }

  storage_for<non_atomic_obj> store;
  auto* raw = store.construct(0);
  metl::intrusive_ptr<non_atomic_obj> p(raw, metl::retain_ref);
  if (!p || !p.has_value()) {
    return 801;
  }
  p.reset();
  return 0;
}

// Compile-time checks for [[nodiscard]] and noexcept-ness of swap.
static_assert(noexcept(std::declval<metl::intrusive_ptr<non_atomic_obj>&>().swap(
                  std::declval<metl::intrusive_ptr<non_atomic_obj>&>())),
              "swap must be noexcept");
static_assert(std::is_nothrow_move_constructible_v<metl::intrusive_ptr<non_atomic_obj>>,
              "move construction must be noexcept");
static_assert(std::is_nothrow_move_assignable_v<metl::intrusive_ptr<non_atomic_obj>>,
              "move assignment must be noexcept");

int main() {
  if (int r = legacy_test())
    return r;
  if (int r = crtp_non_atomic_test())
    return r;
  if (int r = crtp_atomic_test())
    return r;
  if (int r = comparison_test())
    return r;
  if (int r = hash_test())
    return r;
  if (int r = swap_test())
    return r;
  if (int r = converting_ctor_test())
    return r;
  if (int r = detach_test())
    return r;
  if (int r = bool_test())
    return r;
  return 0;
}
