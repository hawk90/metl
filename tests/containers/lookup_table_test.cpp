#include "metl_check.hpp"

#include <array>

#include <metl/lookup_table.hpp>

namespace {

constexpr auto commands =
    metl::make_lookup_table<int, const char*, 3>(std::array<metl::lookup_entry<int, const char*>, 3>{{
        {1, "start"},
        {2, "stop"},
        {3, "reset"},
    }});

constexpr bool constexpr_checks() {
  return commands.size() == 3 && commands.contains(2) && !commands.contains(4) && commands.contains(1) &&
         commands.value_or(3, "none")[0] == 'r' && commands.value_or(9, "none")[0] == 'n';
}

static_assert(constexpr_checks(), "lookup_table constexpr checks failed");

}  // namespace

int main() {
  if (commands.empty() || commands[0].key != 1 || commands[1].value[1] != 't') {
    return 1;
  }

  const auto* found = commands.find(2);
  if (found == nullptr || (*found)[0] != 's') {
    return 2;
  }

  const auto* missing = commands.find(7);
  if (missing != nullptr) {
    return 3;
  }

  if (commands.value_or(7, "fallback")[0] != 'f') {
    return 4;
  }

  constexpr auto empty = metl::make_lookup_table<int, int, 0>(std::array<metl::lookup_entry<int, int>, 0>{});
  if (!empty.empty() || empty.size() != 0) {
    return 5;
  }

  // The same queries at run time. Everything above is a constant expression,
  // so the compiler folds it and none of these functions ever executes; a
  // table built from run-time values makes them run.
  {
    volatile int seed = 10;
    const int base = seed;
    const metl::lookup_table<int, int, 3> runtime(std::array<metl::lookup_entry<int, int>, 3>{{
        {base, base * 2},
        {base + 1, base * 3},
        {base + 2, base * 4},
    }});
    CHECK(!runtime.empty());
    CHECK_EQ(runtime.size(), std::size_t{3});
    CHECK_EQ(runtime[0].key, 10);
    CHECK_EQ(runtime[2].value, 40);
    CHECK_EQ(runtime.at(1).value, 30);
    CHECK(runtime.contains(11));
    CHECK(runtime.contains(12));
    CHECK(!runtime.contains(13));
    CHECK(!runtime.contains(9));
    CHECK_DEREF_EQ(runtime.find(12), 40);
    CHECK_EQ(runtime.value_or(99, -1), -1);

    const metl::lookup_table<int, int, 0> none{};
    CHECK(none.empty());
    CHECK_EQ(none.size(), std::size_t{0});
    CHECK(!none.contains(base));
  }

  return metl_test::exit_code();
}
