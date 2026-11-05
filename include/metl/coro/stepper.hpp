#pragma once

/// @file
/// @brief Progress guarantees for `metl::coro::stepper` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | state queries | wait-free, bounded |
///   | `poll`, `reset` | wait-free, bounded, plus the `step()` / `on_reset()` they call |
///   | the step body itself, and `on_reset()` | **bounded by the body you write** |
///
/// `poll` is one virtual call to `step()` plus a state update; `reset` is a flag
/// clear plus one virtual call to `on_reset()`. Everything else is the work you
/// put in `step()` and `on_reset()`, which this header cannot bound.

// Explicit-state stackless task. Unlike `protothread`, no macro magic: the
// derived class implements `step()` as an explicit state machine and returns
// one of `step_result::yield`, `done`, or `error`. The wrapper `poll()`
// tracks the terminal state so the scheduler can shed finished tasks.
//
// Constraints: noexcept, no heap, no exceptions, no RTTI. The base class does
// use a virtual `step()` because the API is intentionally polymorphic, but
// it has no allocations and the dtor is virtual + noexcept.

#include "metl/compiler.hpp"
#include "metl/detail/abi.hpp"

#include <cstddef>

namespace metl {
METL_DETAIL_ABI_BEGIN
namespace coro {

/// @brief Outcome of one `stepper::step()` call.
enum class step_result : unsigned char {
  yield,  ///< Not done; call again later.
  done,   ///< Finished successfully.
  error,  ///< Finished with error (user-defined semantics).
};

/// @brief Explicit-state stackless cooperative task (C++17, no `<coroutine>`).
///
/// Unlike `protothread`, there is no macro magic: the derived class implements
/// `step()` as an explicit state machine returning a `step_result`. The wrapper
/// `poll()` tracks the terminal state so a scheduler can shed finished tasks.
/// Intended for a single-threaded cooperative run loop; `step()` must be
/// non-blocking. noexcept, no heap, no exceptions, no RTTI.
///
/// A stepper lives in static or automatic storage, in a METL pool or arena, or
/// in storage the caller manages with `::new (p) T`. It is never deleted
/// through a pointer and never made by a plain `new`: neither `delete p` on a
/// `stepper*` or a derived pointer nor `new T` compiles (GCC also rejects an
/// unqualified placement `new (p) T`; write `::new`). That keeps the global
/// `operator delete` (and on newlib, `malloc`) out of the image, which the virtual destructor's deleting form
/// would otherwise link once the vtable is used.
class stepper {
 public:
  stepper() noexcept = default;
  // Never on the heap: see the class comment and the operator delete below.
  static void* operator new(std::size_t) = delete;
  stepper(const stepper&) = delete;
  stepper& operator=(const stepper&) = delete;
  stepper(stepper&&) noexcept = default;
  stepper& operator=(stepper&&) noexcept = default;
  virtual ~stepper() = default;

  /// @brief Run one step of the task's state machine.
  /// @return `yield` (more work), `done` (finished), or `error` (failed).
  METL_NODISCARD virtual step_result step() noexcept = 0;

  /// @brief Whether the task has finished (successfully or with error).
  METL_NODISCARD bool is_done() const noexcept { return done_; }
  /// @brief Whether the task finished with an error.
  METL_NODISCARD bool is_error() const noexcept { return error_; }

  /// @brief Drive `step()` once and update the done/error flags.
  /// @return true if more work remains; false once the task is finished.
  bool poll() noexcept {
    if (done_ || error_) {
      return false;
    }
    const step_result r = step();
    if (r == step_result::done) {
      done_ = true;
      return false;
    }
    if (r == step_result::error) {
      error_ = true;
      done_ = true;
      return false;
    }
    return true;
  }

  /// @brief Clear the done/error flags and reset derived state via `on_reset()`.
  void reset() noexcept {
    done_ = false;
    error_ = false;
    on_reset();
  }

 protected:
  // The deleting destructor that the virtual destructor emits calls this, not
  // the global operator delete, so no heap is linked (#548). It never runs:
  // being protected, it rejects `delete` outside the class. clang-tidy's
  // cert-dcl54-cpp does not count the deleted operator new above as its pair.
  // NOLINTNEXTLINE(cert-dcl54-cpp,misc-new-delete-overloads)
  static void operator delete(void*) noexcept {}

  /// @brief Hook to reset derived state on `reset()`. Default is a no-op.
  virtual void on_reset() noexcept {}

 private:
  bool done_ = false;
  bool error_ = false;
};

}  // namespace coro
METL_DETAIL_ABI_END
}  // namespace metl
