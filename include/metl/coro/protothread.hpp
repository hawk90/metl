#pragma once

/// @file
/// @brief Progress guarantees for `metl::coro::protothread` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | resuming at a yield point, `reset`, `is_done` | wait-free, bounded |
///   | the protothread body itself | **bounded by the body you write** |
///
/// A resume is a `switch` on the saved line number -- a jump table, not a scan --
/// so re-entering a protothread costs the same no matter how many yield points it
/// has. What runs after the jump is your code, and this header cannot bound it. A
/// protothread that loops without yielding blocks its scheduler for as long as the
/// loop runs; the yield macros exist so that it does not have to.

// Stackless cooperative thread, modeled after Adam Dunkels' Protothreads
// (https://dunkels.com/adam/pt/). Uses Duff's-device style yield via a
// `switch` over `__LINE__` markers. State is a single `int` (`pt_line_`):
//
//     0   = initial / start
//    -1   = finished
//   else  = `__LINE__` where the thread suspended
//
// Convention:
//   - Derive from `metl::coro::protothread`.
//   - Implement `bool run() noexcept`.
//   - Inside `run()`, wrap the body with `METL_PT_BEGIN()` / `METL_PT_END()`.
//   - `run()` returns `false` on yield (still running) and `true` on completion.
//
// Limitation (inherited from the Protothreads design):
//   - Yield macros expand to `case __LINE__:` labels. Therefore no two yield
//     sites may appear on the same source line. Put each yield on its own line.
//   - Locals are not preserved across yields: store state in class members.
//
// All operations are noexcept; the type has no virtuals, no allocations, and
// is freestanding-safe.

#include "metl/compiler.hpp"

namespace metl {
namespace coro {

/// @brief Stackless cooperative task (C++17, no `<coroutine>`).
///
/// A Duff's-device style protothread whose suspend/resume state is a single
/// `int` (`pt_line_`). Derive from this, implement `bool run() noexcept`, and
/// wrap the body in `METL_PT_BEGIN()` / `METL_PT_END()`. `run()` returns false
/// on yield (still running) and true on completion. Runs on a single-threaded
/// cooperative loop; the body must be non-blocking.
/// @note Locals are not preserved across yields — store state in members. No two
///       yield sites may share a source line (each expands to a `case` label).
class protothread {
 public:
  protothread() noexcept = default;
  protothread(const protothread&) = delete;
  protothread& operator=(const protothread&) = delete;
  protothread(protothread&&) noexcept = default;
  protothread& operator=(protothread&&) noexcept = default;
  ~protothread() = default;

  /// @brief Whether the task has finished. @return true once the body ran to end.
  METL_NODISCARD bool is_done() const noexcept { return pt_line_ < 0; }
  /// @brief Restart the task from the beginning on the next `run()`.
  void reset() noexcept { pt_line_ = 0; }

 protected:
  /// @brief Resume point: 0 = start, -1 = finished, else the yield's `__LINE__`.
  int pt_line_ = 0;
};

}  // namespace coro
}  // namespace metl

// ---------------------------------------------------------------------------
// Protothread macros.
//
// These must be used inside a member function (conventionally `bool run()`) of
// a class derived from `metl::coro::protothread`. They read and mutate
// `this->pt_line_`. The enclosing function must return `bool`.
// ---------------------------------------------------------------------------

/// @brief Open a protothread body: dispatches to the last suspend point.
///        Must pair with `METL_PT_END()` and be used inside `bool run()`.
#define METL_PT_BEGIN()           \
  bool METL_PT_yielded__ = false; \
  (void)METL_PT_yielded__;        \
  switch (this->pt_line_) {       \
    case 0:

/// @brief Suspend the task, returning control to the caller; resumes here next
///        `run()`. Each yield must be on its own source line.
#define METL_PT_YIELD()        \
  do {                         \
    this->pt_line_ = __LINE__; \
    return false;              \
    case __LINE__:;            \
  } while (0)

/// @brief Suspend and resume repeatedly until `cond` becomes true, then continue.
#define METL_PT_WAIT_UNTIL(cond) \
  do {                           \
    this->pt_line_ = __LINE__;   \
    [[fallthrough]];             \
    case __LINE__:               \
      if (!(cond)) {             \
        return false;            \
      }                          \
  } while (0)

/// @brief Suspend and resume repeatedly while `cond` stays true, then continue.
#define METL_PT_WAIT_WHILE(cond) METL_PT_WAIT_UNTIL(!(cond))

/// @brief Restart the body from the beginning and yield (keeps the task running).
#define METL_PT_RESTART() \
  do {                    \
    this->pt_line_ = 0;   \
    return false;         \
  } while (0)

/// @brief Terminate the task early, marking it done (`run()` returns true).
#define METL_PT_EXIT()   \
  do {                   \
    this->pt_line_ = -1; \
    return true;         \
  } while (0)

/// @brief Close a protothread body: marks the task done and returns true.
#define METL_PT_END()  \
  }                    \
  this->pt_line_ = -1; \
  return true
