#pragma once

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

class protothread {
 public:
  protothread() noexcept = default;
  protothread(const protothread&) = delete;
  protothread& operator=(const protothread&) = delete;
  protothread(protothread&&) noexcept = default;
  protothread& operator=(protothread&&) noexcept = default;
  ~protothread() = default;

  METL_NODISCARD bool is_done() const noexcept { return pt_line_ < 0; }
  void reset() noexcept { pt_line_ = 0; }

 protected:
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

#define METL_PT_BEGIN()           \
  bool METL_PT_yielded__ = false; \
  (void)METL_PT_yielded__;        \
  switch (this->pt_line_) {       \
    case 0:

#define METL_PT_YIELD()        \
  do {                         \
    this->pt_line_ = __LINE__; \
    return false;              \
    case __LINE__:;            \
  } while (0)

#define METL_PT_WAIT_UNTIL(cond) \
  do {                           \
    this->pt_line_ = __LINE__;   \
    case __LINE__:               \
      if (!(cond)) {             \
        return false;            \
      }                          \
  } while (0)

#define METL_PT_WAIT_WHILE(cond) METL_PT_WAIT_UNTIL(!(cond))

#define METL_PT_RESTART() \
  do {                    \
    this->pt_line_ = 0;   \
    return false;         \
  } while (0)

#define METL_PT_EXIT()   \
  do {                   \
    this->pt_line_ = -1; \
    return true;         \
  } while (0)

#define METL_PT_END()  \
  }                    \
  this->pt_line_ = -1; \
  return true
