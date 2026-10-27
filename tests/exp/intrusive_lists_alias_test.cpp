// metl/exp/intrusive_list.hpp and metl/exp/intrusive_forward_list.hpp (#520):
// the metl::exp names are the metl:: types, not copies of them, so code that
// still names them links into the same lists as code that does not.

#include "metl_check.hpp"

#include <type_traits>

#include <metl/exp/intrusive_forward_list.hpp>
#include <metl/exp/intrusive_list.hpp>

namespace {

struct tag {};

struct node : metl::exp::list_hook<>, metl::exp::forward_list_hook<tag> {
  int value = 0;
};

static_assert(std::is_same<metl::exp::list_hook<>, metl::list_hook<>>::value, "");
static_assert(std::is_same<metl::exp::forward_list_hook<tag>, metl::forward_list_hook<tag>>::value, "");
static_assert(std::is_same<metl::exp::intrusive_list<node>, metl::intrusive_list<node>>::value, "");
static_assert(std::is_same<metl::exp::intrusive_forward_list<node, tag>,
                           metl::intrusive_forward_list<node, tag>>::value,
              "");

}  // namespace

int main() {
  node a;
  node b;
  {
    metl::intrusive_list<node> list;
    metl::exp::intrusive_forward_list<node, tag> forward;
    list.push_back(a);
    list.push_back(b);
    forward.push_front(b);
    CHECK_EQ(list.size(), 2u);
    CHECK(b.metl::forward_list_hook<tag>::is_linked());
    CHECK_EQ(&forward.front(), &b);
  }
  CHECK(!a.metl::list_hook<>::is_linked());
  CHECK(!b.metl::forward_list_hook<tag>::is_linked());
  return metl_test::exit_code();
}
