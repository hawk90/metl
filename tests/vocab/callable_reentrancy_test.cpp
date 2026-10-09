// fixed_function / fixed_any_invocable when the stored target reaches back
// into the wrapper holding it.
//
//   * Assignment ran reset() and then constructed from the argument. A stage
//     that replaces itself with a member it carries (`slot = next_;`) passed a
//     reference into the target being destroyed, so the next stage was copied
//     from a dead object: it ran with v = -999 where std::function gives 42.
//     The new target is now built before the old one is released.
//   * reset() destroyed the target and only then marked the wrapper empty, so a
//     target whose destructor reset the wrapper destroyed itself twice.

#include "metl_check.hpp"

#include <metl/fixed_function.hpp>

namespace {

struct next_stage {
  int value;
  explicit next_stage(int v) noexcept : value(v) {}
  next_stage(const next_stage&) noexcept = default;
  next_stage(next_stage&&) noexcept = default;
  next_stage& operator=(const next_stage&) noexcept = default;
  next_stage& operator=(next_stage&&) noexcept = default;
  // Scribbles on destruction, so a copy made from a destroyed stage shows.
  ~next_stage() { value = -999; }
  int operator()() const noexcept { return value; }
};

metl::fixed_function<int(), 32>& pipeline() {
  static metl::fixed_function<int(), 32> slot;
  return slot;
}

struct first_stage {
  next_stage next{42};
  int operator()() noexcept {
    pipeline() = next;  // replaces the target that is running
    return 0;
  }
};

metl::fixed_any_invocable<int(), 32>& invocable_pipeline() {
  static metl::fixed_any_invocable<int(), 32> slot;
  return slot;
}

struct first_invocable_stage {
  next_stage next{7};
  int operator()() noexcept {
    invocable_pipeline() = static_cast<next_stage&&>(next);
    return 0;
  }
};

int g_destroyed = 0;

struct resets_on_destroy {
  bool armed = true;
  resets_on_destroy() = default;
  resets_on_destroy(const resets_on_destroy&) noexcept = default;
  resets_on_destroy(resets_on_destroy&& other) noexcept : armed(other.armed) { other.armed = false; }
  ~resets_on_destroy() {
    if (armed) {
      ++g_destroyed;
      pipeline().reset();  // the wrapper must already be empty
    }
  }
  int operator()() const noexcept { return 1; }
};

}  // namespace

int main() {
  // Self-replacement from inside the running target.
  pipeline() = first_stage{};
  CHECK_EQ(pipeline()(), 0);
  CHECK_EQ(pipeline()(), 42);

  invocable_pipeline() = first_invocable_stage{};
  CHECK_EQ(invocable_pipeline()(), 0);
  CHECK_EQ(invocable_pipeline()(), 7);

  // A target whose destructor resets the wrapper holding it.
  pipeline() = resets_on_destroy{};
  g_destroyed = 0;
  pipeline().reset();
  CHECK_EQ(g_destroyed, 1);
  CHECK(!pipeline().has_value());

  return metl_test::exit_code();
}
