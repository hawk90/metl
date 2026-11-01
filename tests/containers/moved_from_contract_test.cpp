// What a moved-from container holds (#559): the contract its header states.
// fixed_string keeps its contents; every other fixed-capacity container is left
// empty and usable.

#include "metl_check.hpp"

#include <cstddef>
#include <utility>

#include <metl/fixed_deque.hpp>
#include <metl/fixed_priority_queue.hpp>
#include <metl/fixed_queue.hpp>
#include <metl/fixed_stack.hpp>
#include <metl/fixed_string.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/static_message_queue.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

template <typename Container, typename Fill>
void check(Fill fill, std::size_t left_behind) {
  Container constructed_from;
  CHECK(fill(constructed_from));
  const Container constructed(std::move(constructed_from));
  CHECK_EQ(constructed.size(), std::size_t{1});
  CHECK_EQ(constructed_from.size(), left_behind);

  Container assigned_from;
  CHECK(fill(assigned_from));
  Container assigned;
  assigned = std::move(assigned_from);
  CHECK_EQ(assigned.size(), std::size_t{1});
  CHECK_EQ(assigned_from.size(), left_behind);

  // Usable: the source takes an element again.
  if (left_behind == 0) {
    CHECK(fill(assigned_from));
    CHECK_EQ(assigned_from.size(), std::size_t{1});
  }
}

}  // namespace

int main() {
  const auto push_back = [](auto& c) { return c.try_push_back(1); };
  const auto push = [](auto& c) { return c.try_push(1); };
  const auto emplace_key = [](auto& c) { return c.try_emplace(1); };
  const auto emplace_pair = [](auto& c) { return c.try_emplace(1, 1); };

  check<metl::fixed_vector<int, 4>>(push_back, 0);
  check<metl::ring_buffer<int, 4>>(push_back, 0);
  check<metl::fixed_deque<int, 4>>(push_back, 0);
  check<metl::fixed_queue<int, 4>>(push, 0);
  check<metl::fixed_stack<int, 4>>(push, 0);
  check<metl::fixed_priority_queue<int, 4>>(push, 0);
  check<metl::static_message_queue<int, 4>>(push, 0);
  check<metl::flat_map<int, int, 4>>(emplace_pair, 0);
  check<metl::flat_set<int, 4>>(emplace_key, 0);
  check<metl::static_unordered_map<int, int, 4>>(emplace_pair, 0);
  check<metl::static_unordered_set<int, 4>>(emplace_key, 0);
  check<metl::fixed_string<4>>([](auto& c) { return c.try_push_back('a'); }, 1);
  return metl_test::exit_code();
}
