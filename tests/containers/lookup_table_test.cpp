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
  return commands.size() == 3 && commands.contains(2) && !commands.contains(4) &&
         commands.find(1) != nullptr && commands.value_or(3, "none")[0] == 'r' &&
         commands.value_or(9, "none")[0] == 'n';
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

  return 0;
}
