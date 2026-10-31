// metl/exp/once_flag.hpp (#524): metl::exp::once_flag is metl::once_flag, not
// a copy of it, so a flag declared under either name is claimed once by
// try_call_once called under either name.

#include "metl_check.hpp"

#include <type_traits>

#include <metl/exp/once_flag.hpp>
#include <metl/lock.hpp>

namespace {

static_assert(std::is_same<metl::exp::once_flag<>, metl::once_flag<>>::value, "");
static_assert(std::is_same<metl::exp::once_flag<metl::null_lock>, metl::once_flag<metl::null_lock>>::value,
              "");

}  // namespace

int main() {
  metl::exp::once_flag<metl::null_lock> flag;
  int runs = 0;
  CHECK(metl::exp::try_call_once(flag, [&] { ++runs; }));
  CHECK(metl::try_call_once(flag, [&] { ++runs; }));
  CHECK(flag.is_done());
  CHECK_EQ(runs, 1);
  return metl_test::exit_code();
}
