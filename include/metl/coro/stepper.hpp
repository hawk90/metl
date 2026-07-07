#pragma once

// Explicit-state stackless task. Unlike `protothread`, no macro magic: the
// derived class implements `step()` as an explicit state machine and returns
// one of `step_result::yield`, `done`, or `error`. The wrapper `poll()`
// tracks the terminal state so the scheduler can shed finished tasks.
//
// Constraints: noexcept, no heap, no exceptions, no RTTI. The base class does
// use a virtual `step()` because the API is intentionally polymorphic, but
// it has no allocations and the dtor is virtual + noexcept.

#include "metl/compiler.hpp"

namespace metl {
namespace coro {

enum class step_result : unsigned char {
  yield,  // not done, call again later
  done,   // finished successfully
  error,  // finished with error (user-defined semantics)
};

class stepper {
 public:
  stepper() noexcept = default;
  stepper(const stepper&) = delete;
  stepper& operator=(const stepper&) = delete;
  stepper(stepper&&) noexcept = default;
  stepper& operator=(stepper&&) noexcept = default;
  virtual ~stepper() = default;

  // Run one step. Returns yield (not done), done (finished), or error.
  METL_NODISCARD virtual step_result step() noexcept = 0;

  METL_NODISCARD bool is_done() const noexcept { return done_; }
  METL_NODISCARD bool is_error() const noexcept { return error_; }

  // Drive `step()` once and update flags. Returns true if more work remains.
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

  void reset() noexcept {
    done_ = false;
    error_ = false;
    on_reset();
  }

 protected:
  // Override to reset derived state. Default = noop.
  virtual void on_reset() noexcept {}

 private:
  bool done_ = false;
  bool error_ = false;
};

}  // namespace coro
}  // namespace metl
