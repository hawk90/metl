#pragma once

#include "metl/assert.hpp"
#include "metl/attributes.hpp"
#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/type_traits.hpp"
#include "metl/versioned_handle.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <utility>
#if __cplusplus >= 202002L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
#include <bit>  // std::bit_cast, for the constant-initializing constructor
#endif

namespace metl {
METL_DETAIL_ABI_BEGIN

/// @brief Slot-based object pool addressed by generation-tagged handles.
///
/// Same fixed-capacity, zero-heap model as `object_pool`, with one difference
/// that follows from replacing the raw pointer with a `versioned_handle`:
/// **use-after-free is detected, not undefined.** Every slot carries a
/// generation counter that is bumped on destroy, so a handle to a destroyed
/// object no longer matches its slot and `get()` returns `nullptr` instead of a
/// dangling pointer into a recycled slot. This also gives a weak reference for
/// free: hold the handle, check on each use.
///
/// Both pools allocate in O(1) from an intrusive free list, so capacity does
/// not decide between them. Measured with `bench/bench_pools.cpp` on an arm64
/// host (Apple clang, -O2), allocate-and-free churn at the tail:
///
///   | Capacity | `object_pool` | `handle_pool` |
///   |----------|---------------|---------------|
///   | 4        | 2.7 ns        | 1.8 ns        |
///   | 64       | 4.0 ns        | 2.5 ns        |
///   | 1024     | 4.0 ns        | 2.5 ns        |
///
/// A `get()` that checks the generation costs about 0.4 ns. Pick on the
/// property you need: a raw pointer, or a handle that detects reuse.
///
/// The handle is 32 bits by default (16-bit index + 16-bit generation) and
/// makes no assumption whatsoever about pointer representation, which is what
/// makes it portable to targets where the spare bits of a pointer belong to the
/// hardware (AArch64 PAC/MTE, x86-64 LA57/LAM). See versioned_handle.hpp.
///
/// Progress guarantees:
///
///   | Operation                   | Guarantee            |
///   |-----------------------------|----------------------|
///   | `try_emplace` / `destroy`   | wait-free, bounded, **plus `T`'s constructor / `~T()`** |
///   | `get` / `contains`          | wait-free, bounded   |
///   | constructor / `clear`       | bounded, O(Capacity), **plus `~T()` for each live object in `clear`** |
///
/// @par Reference invalidation
/// Objects never move: `try_emplace`/`emplace` invalidate nothing, and
/// `destroy` ends only the object its handle names. That handle stops resolving
/// (its slot's generation changes); other handles, and pointers to other
/// objects, stay valid. `clear` ends them all.
///
/// @tparam T Pooled object type.
/// @tparam Capacity Number of slots (fixed at compile time).
/// @tparam GenT Unsigned generation counter type. Widening it to `std::uint32_t`
///         costs 2 bytes per slot and pushes the handle to 64 bits.
///
/// @warning **Generation wraparound.** Issued generations are the odd values
///          of `GenT` (an even one marks the slot free), so after
///          `2^(8*sizeof(GenT) - 1)` destroy cycles on the *same slot*,
///          generations repeat and a handle retained across all of them can
///          alias a new object. With the default 16-bit counter that is 32768
///          cycles per slot. This is inherent to generation tagging; use a
///          wider `GenT` when handles are held for unbounded periods. A 32-bit
///          counter gives 2^31 cycles per slot. Freed slots are reused oldest
///          first, so steady churn of one object walks every slot in turn and
///          a slot wraps only after all of them have taken as many cycles (#642).
///
/// @warning **A handle names a slot, not a pool.** A handle from one
///          `handle_pool<T, N>` is also accepted by every other pool of the same
///          type, and resolves to whatever that pool holds in the same slot with
///          the same generation (#316). Two pools of one type that hand out
///          handles to the same code need a wrapper type, or a pool id kept
///          beside the handle.
///
/// @note Not thread-safe. Concurrent use needs external synchronisation. No
///       lock-free variant is planned; `metl::atomic_handle` (atomic_handle.hpp)
///       is the capability-gated building block for one (it needs a single-word
///       CAS, which Cortex-M0 lacks).
///
/// @see object_pool for the pointer-addressed equivalent.
template <typename T, std::size_t Capacity, typename GenT = std::uint16_t>
class handle_pool {
  static_assert(std::is_object_v<T> && !std::is_array_v<T>, METL_DETAIL_OBJECT_TYPE_MESSAGE);

 public:
  using value_type = T;
  using size_type = std::size_t;
  using index_type = std::uint16_t;
  using generation_type = GenT;
  using pointer = T*;
  using const_pointer = const T*;

  /// Handle type issued by this pool. Tagged with the pool type, so a handle
  /// from a differently-parameterised pool is a compile error rather than a
  /// silent index mix-up.
  using handle_type = versioned_handle<handle_pool, index_type, GenT>;

  /// Largest `Capacity` a slot index can address, derived from the handle's own
  /// index field so the two can never drift apart.
  static constexpr size_type max_capacity = static_cast<size_type>(handle_type::max_index);

  static_assert(Capacity <= max_capacity,
                "metl::handle_pool Capacity must fit the handle's slot index field");

  /// Constructs an empty pool with every slot free and generation 1.
  /// @note Not constexpr: the free list is linked at run time, so a
  ///       namespace-scope pool is initialized at run time, in link order.
  ///       Construct it before anything that runs earlier can reach it, or,
  ///       since C++20, use the `constant_init` constructor below.
  handle_pool() noexcept : free_head_(0), size_(0) {
    for (size_type i = 0; i < Capacity; ++i) {
      set_link(i, i + 1);                   // last one points at `Capacity` == end
      generation_[i] = generation_type{0};  // even: free (see generation_)
    }
  }

#if defined(__cpp_lib_bit_cast) && defined(__cpp_constexpr) && __cpp_constexpr >= 201907L
  /// @brief Tag selecting the constant-initializing constructor.
  struct constant_init_t {
    explicit constant_init_t() = default;
  };
  /// @brief The tag value: `handle_pool<T, N> pool{handle_pool<T, N>::constant_init};`.
  static constexpr constant_init_t constant_init{};

  /// @brief Constructs the same empty pool as the default constructor, during
  ///        constant evaluation (C++20), so a namespace-scope pool declared
  ///        `constinit` is ready before any code runs, including an interrupt
  ///        enabled by another file's constructor (#408).
  ///
  /// The empty free list links every slot to the next, which is not all-zero,
  /// so a pool built this way is stored in `.data`: it takes `sizeof` bytes of
  /// flash, where a default-constructed one is in `.bss`. Use it for a pool that
  /// something can reach before `main()`; elsewhere the default constructor
  /// costs no flash. Construction and every operation behave as for a
  /// default-constructed pool.
  constexpr explicit handle_pool(constant_init_t) noexcept
      : storage_{}, generation_{}, free_head_(0), size_(0) {
    for (size_type i = 0; i < Capacity; ++i) {
      // The bytes `set_link(i, i + 1)` would write, in this target's byte order.
      struct link_bytes {
        unsigned char bytes[sizeof(index_type)];
      };
      const auto link = std::bit_cast<link_bytes>(static_cast<index_type>(i + 1));
      for (size_type b = 0; b < sizeof(index_type); ++b) {
        storage_[i].bytes[b] = link.bytes[b];
      }
    }
  }
#endif

  ~handle_pool() { clear(); }

  handle_pool(const handle_pool&) = delete;
  handle_pool& operator=(const handle_pool&) = delete;
  handle_pool(handle_pool&&) = delete;
  handle_pool& operator=(handle_pool&&) = delete;

  /// Constructs an object in a free slot.
  /// @return Handle to the new object, or a null handle if the pool is full
  ///         (no assert).
  ///
  /// The slot leaves the free list before T's constructor runs, so a
  /// constructor that emplaces into this pool gets a different slot; if the
  /// constructor throws, the slot goes back.
  template <typename... Args>
  METL_NODISCARD handle_type try_emplace(Args&&... args) {
    if (free_head_ >= Capacity) {
      return handle_type{};
    }

    const size_type index = free_head_;
    free_head_ = link(index);
    generation_[index] = static_cast<generation_type>(generation_[index] + generation_type{1});  // odd: live
    ++size_;
    return_on_unwind claim{this, index};
    ::new (storage_[index].addr()) T(std::forward<Args>(args)...);
    claim.dismiss();
    return handle_type{detail::issued_handle, static_cast<index_type>(index), generation_[index]};
  }

  /// Constructs an object in a free slot and returns its handle.
  /// @pre Pool is not full; a full pool asserts and aborts. Use `try_emplace`
  ///      for a non-asserting path.
  template <typename... Args>
  METL_NODISCARD handle_type emplace(Args&&... args) {
    handle_type handle = try_emplace(std::forward<Args>(args)...);
    METL_ASSERT(handle.valid());
    return handle;
  }

  /// Resolves a handle to the object it refers to.
  /// @return Pointer to the live object, or nullptr if the handle is null,
  ///         out of range, or **stale** (its slot has since been recycled).
  METL_NODISCARD pointer get(handle_type handle) noexcept {
    const size_type index = live_index(handle);
    return index < Capacity ? slot_ptr(index) : nullptr;
  }

  /// @copydoc get
  METL_NODISCARD const_pointer get(handle_type handle) const noexcept {
    const size_type index = live_index(handle);
    return index < Capacity ? slot_ptr(index) : nullptr;
  }

  /// Returns true if `handle` refers to a live object in this pool.
  METL_NODISCARD bool contains(handle_type handle) const noexcept { return live_index(handle) < Capacity; }

  /// Destroys the object a handle refers to and returns its slot to the pool.
  /// @return true if destroyed; false if the handle was null or already stale
  ///         — so a double-destroy is reported, not undefined.
  bool destroy(handle_type handle) noexcept {
    const size_type index = live_index(handle);
    if (index >= Capacity) {
      return false;
    }

    release(index);
    return true;
  }

  /// Destroys every live object and frees every slot. All outstanding handles
  /// become stale.
  void clear() noexcept {
    for (size_type i = 0; i < Capacity; ++i) {
      if ((generation_[i] & generation_type{1}) != 0) {
        release(i);
      }
    }
  }

  /// Returns true if no slots are in use.
  METL_NODISCARD bool empty() const noexcept { return size_ == 0; }
  /// Returns true if every slot is in use.
  METL_NODISCARD bool full() const noexcept { return size_ == Capacity; }
  /// Returns the number of live objects.
  METL_NODISCARD size_type size() const noexcept { return size_; }
  /// Returns the fixed slot count (`Capacity`).
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }
  /// Returns the number of free slots (`Capacity - size()`).
  METL_NODISCARD size_type available() const noexcept { return Capacity - size_; }

  /// Returns the generation of a slot's handle: the one a live object's handle
  /// carries, or, for a free slot, the one its next handle will carry. Never 0,
  /// and 1 for a slot that was never used. Exposed for diagnostics and for
  /// tests that need to drive the counter to wraparound.
  METL_NODISCARD generation_type generation_of(index_type index) const noexcept {
    METL_ASSERT(index < Capacity);
    return static_cast<generation_type>(generation_[index] | generation_type{1});
  }

 private:
  // A slot holds a T while live and its free-list link while free, in the
  // same bytes, so it is sized for whichever is larger.
  struct storage_type {
    alignas(T) unsigned char bytes[sizeof(T) < sizeof(index_type) ? sizeof(index_type) : sizeof(T)];
    T* ptr() noexcept { return std::launder(reinterpret_cast<T*>(&bytes[0])); }
    const T* ptr() const noexcept { return std::launder(reinterpret_cast<const T*>(&bytes[0])); }
    void* addr() noexcept { return &bytes[0]; }
  };

  // The free-list link of a free slot, read and written as bytes: no object
  // lives there, and the bytes may not be aligned for index_type.
  size_type link(size_type index) const noexcept {
    index_type next = 0;
    std::memcpy(&next, &storage_[index].bytes[0], sizeof next);
    return next;
  }
  void set_link(size_type index, size_type next) noexcept {
    const auto value = static_cast<index_type>(next);
    std::memcpy(&storage_[index].bytes[0], &value, sizeof value);
  }

  pointer slot_ptr(size_type index) noexcept { return storage_[index].ptr(); }
  const_pointer slot_ptr(size_type index) const noexcept { return storage_[index].ptr(); }

  /// Returns the slot index a handle resolves to, or `Capacity` when it does
  /// not resolve. One place where liveness is decided, so `get`, `contains` and
  /// `destroy` cannot drift apart.
  size_type live_index(handle_type handle) const noexcept {
    // An issued handle's generation is odd. The test also rejects the null
    // handle, whose generation is 0, and a handle forged with an even
    // generation, which would otherwise match a free slot.
    if ((handle.generation() & generation_type{1}) == 0) {
      return Capacity;
    }

    const size_type index = handle.index();
    if (index >= Capacity || generation_[index] != handle.generation()) {
      return Capacity;
    }

    return index;
  }

  /// Destroys the object in `index` and pushes the slot onto the free list,
  /// bumping the generation so every outstanding handle to it goes stale.
  ///
  /// Order matters when ~T() calls back into this pool. The handle goes stale
  /// before the destructor runs, so a re-entrant destroy() of it reports false
  /// instead of destroying twice; the slot joins the free list only after, so
  /// a re-entrant try_emplace() cannot build over the object being destroyed.
  void release(size_type index) noexcept {
    // Odd to even: the slot stops being live and every handle to it goes
    // stale. An even generation is never issued, so wrapping to 0 is harmless.
    generation_[index] = static_cast<generation_type>(generation_[index] + generation_type{1});
    --size_;

    slot_ptr(index)->~T();

    append_free(index);
  }

  // The free list is FIFO: a freed slot goes to the back, so the slot reused
  // next is the one freed longest ago, and churn spreads its generation bumps
  // over the pool instead of wrapping one slot (#642). A link of `Capacity`
  // ends the list; `free_tail_` is valid while the list is not empty.
  void append_free(size_type index) noexcept {
    set_link(index, Capacity);
    if (free_head_ >= Capacity) {
      free_head_ = index;
    } else {
      set_link(free_tail_, index);
    }
    free_tail_ = index;
  }

  // Puts a claimed slot back on the free list if T's constructor exits by an
  // exception. No handle to it was handed out, so the generation goes back.
  class return_on_unwind {
   public:
    return_on_unwind(handle_pool* pool, size_type index) noexcept : pool_(pool), index_(index) {}
    return_on_unwind(const return_on_unwind&) = delete;
    return_on_unwind& operator=(const return_on_unwind&) = delete;
    return_on_unwind(return_on_unwind&&) = delete;
    return_on_unwind& operator=(return_on_unwind&&) = delete;
    ~return_on_unwind() {
      if (pool_ != nullptr) {
        // Back to the even generation it had: no handle was issued at the odd one.
        pool_->generation_[index_] =
            static_cast<generation_type>(pool_->generation_[index_] - generation_type{1});
        --pool_->size_;
        // To the FRONT: the slot is reused next, as if never claimed.
        if (pool_->free_head_ >= Capacity) {
          pool_->free_tail_ = index_;
        }
        pool_->set_link(index_, pool_->free_head_);
        pool_->free_head_ = index_;
      }
    }
    void dismiss() noexcept { pool_ = nullptr; }

   private:
    handle_pool* pool_ = nullptr;
    size_type index_ = 0;
  };

  // `Capacity == 0 ? 1 : Capacity` throughout: a zero-capacity pool is a valid
  // degenerate case (every other fixed-capacity METL container accepts one), and
  // a zero-length array is not. Every operation still behaves correctly --
  // try_emplace returns a null handle because free_head_ (0) is already >=
  // Capacity, and nothing ever indexes the padding element.
  storage_type storage_[Capacity == 0 ? 1 : Capacity];
  // Odd while the slot is live, even while it is free: the parity is the
  // liveness flag, so a slot needs no flag of its own.
  generation_type generation_[Capacity == 0 ? 1 : Capacity];
  size_type free_head_;
  // The last free slot; the constructors link 0 .. Capacity-1 in order.
  size_type free_tail_ = last_slot;
  size_type size_;
  static constexpr size_type last_slot = Capacity == 0 ? 0 : Capacity - 1;
};

METL_DETAIL_ABI_END
}  // namespace metl
