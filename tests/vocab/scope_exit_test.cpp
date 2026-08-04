#include <metl/scope_exit.hpp>

namespace {

int g_cleanup_count = 0;

void increment_cleanup() noexcept {
  ++g_cleanup_count;
}

}  // namespace

int main() {
  g_cleanup_count = 0;

  // Basic guard fires on scope exit.
  {
    auto guard = metl::make_scope_exit([]() noexcept { ++g_cleanup_count; });
    (void)guard;
  }
  if (g_cleanup_count != 1) {
    return 1;
  }

  // release() disarms the guard.
  {
    auto guard = metl::make_scope_exit([]() noexcept { ++g_cleanup_count; });
    guard.release();
    if (guard.active()) {
      return 2;
    }
  }
  if (g_cleanup_count != 1) {
    return 3;
  }

  // CTAD deduction works on a stack-built guard with function pointer.
  {
    metl::scope_exit guard(&increment_cleanup);
    (void)guard;
  }
  if (g_cleanup_count != 2) {
    return 4;
  }

  // Move transfers ownership; only one fires.
  {
    auto guard_a = metl::make_scope_exit([]() noexcept { ++g_cleanup_count; });
    auto guard_b = static_cast<decltype(guard_a)&&>(guard_a);
    if (guard_a.active()) {
      return 5;
    }
    if (!guard_b.active()) {
      return 6;
    }
  }
  if (g_cleanup_count != 3) {
    return 7;
  }

  // Macro form fires exactly once when the scope exits.
  {
    int local = 0;
    METL_SCOPE_EXIT(local = 42);
    if (local != 0) {
      return 8;
    }
    // The macro variable goes out of scope here, modifying local.
    METL_SCOPE_EXIT(++g_cleanup_count);
  }
  if (g_cleanup_count != 4) {
    return 9;
  }

  // Two macros in same scope should both fire (unique names via __COUNTER__).
  {
    METL_SCOPE_EXIT(++g_cleanup_count);
    METL_SCOPE_EXIT(++g_cleanup_count);
  }
  if (g_cleanup_count != 6) {
    return 10;
  }

  return 0;
}
