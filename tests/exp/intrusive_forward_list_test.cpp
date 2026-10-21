// metl::exp::intrusive_forward_list (#337): every operation against a
// std::vector model of the element order, an element in two lists through two
// hooks, and the three misuses the checked hook exists for, each of which must
// trap: destroying a linked element, linking it a second time, and erasing it
// through a list it does not belong to.

#include "assert_trip.hpp"
#include "metl_check.hpp"

#include <cstddef>
#include <iterator>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#include <metl/attributes.hpp>
#include <metl/exp/intrusive_forward_list.hpp>

namespace {

using metl_test::trips;

struct by_id {};
struct by_age {};

// Two hooks: one element can be in a by_id list and a by_age list at once.
struct timer : metl::exp::forward_list_hook<by_id>, metl::exp::forward_list_hook<by_age> {
  explicit timer(int id_ = 0) : id(id_) {}
  int id;
};

using id_list = metl::exp::intrusive_forward_list<timer, by_id>;
using age_list = metl::exp::intrusive_forward_list<timer, by_age>;

struct plain : metl::exp::forward_list_hook<> {
  int value = 0;
};
using plain_list = metl::exp::intrusive_forward_list<plain>;

// ---- compile time -----------------------------------------------------------

// The measured cost of the check: the hook holds its successor and its owner,
// one pointer more than an unchecked hook.
static_assert(sizeof(metl::exp::forward_list_hook<>) == 2 * sizeof(void*), "next and owner");
static_assert(sizeof(plain_list) == 2 * sizeof(void*) + sizeof(std::size_t), "head link and size");

static_assert(std::is_same<std::iterator_traits<id_list::iterator>::iterator_category,
                           std::forward_iterator_tag>::value,
              "a forward iterator");
static_assert(std::is_same<decltype(*std::declval<id_list::const_iterator>()), const timer&>::value,
              "a const_iterator yields const elements");
static_assert(std::is_convertible<id_list::iterator, id_list::const_iterator>::value,
              "iterator -> const_iterator");
static_assert(!std::is_copy_constructible<id_list>::value && !std::is_move_constructible<id_list>::value,
              "elements record their list's address, so a list stays where it is");
static_assert(std::is_nothrow_default_constructible<metl::exp::forward_list_hook<>>::value, "noexcept hook");

// A namespace-scope list, such as a driver's table of static descriptors, is
// constant-initialized: no constructor runs before main.
METL_CONST_INIT plain_list g_static_list;

// ---- helpers ----------------------------------------------------------------

// Stops after 64 elements: a misuse that got through can link a list into a
// cycle, and the test should then fail rather than hang.
template <typename List>
std::vector<int> ids(const List& list) {
  std::vector<int> out;
  for (const timer& t : list) {
    out.push_back(t.id);
    if (out.size() == 64) {
      break;
    }
  }
  return out;
}

template <typename List>
bool hook_linked(const timer& t) {
  return static_cast<const typename List::hook_type&>(t).is_linked();
}

// ---- operations -------------------------------------------------------------

void empty_list() {
  id_list list;
  CHECK(list.empty());
  CHECK(list.size() == 0);
  CHECK(list.begin() == list.end());
  CHECK(list.cbegin() == list.cend());
  CHECK(std::next(list.before_begin()) == list.end());
}

void push_and_pop_front() {
  timer a(1);
  timer b(2);
  timer c(3);
  id_list list;
  list.push_front(a);
  list.push_front(b);
  list.push_front(c);
  CHECK(ids(list) == (std::vector<int>{3, 2, 1}));
  CHECK(list.size() == 3);
  CHECK(&list.front() == &c);
  CHECK(hook_linked<id_list>(a) && hook_linked<id_list>(b) && hook_linked<id_list>(c));

  list.pop_front();
  CHECK(ids(list) == (std::vector<int>{2, 1}));
  CHECK(!hook_linked<id_list>(c));
  CHECK(list.size() == 2);
  list.pop_front();
  list.pop_front();
  CHECK(list.empty());
  CHECK(!hook_linked<id_list>(a) && !hook_linked<id_list>(b));
}

void insert_after_and_erase_after() {
  timer t[4] = {timer(0), timer(1), timer(2), timer(3)};
  id_list list;
  id_list::iterator last = list.insert_after(list.before_begin(), t[0]);
  CHECK(&*last == &t[0]);
  last = list.insert_after(last, t[1]);
  last = list.insert_after(last, t[3]);
  // In the middle: after 1, before 3.
  id_list::iterator two = list.insert_after(std::next(list.begin()), t[2]);
  CHECK(&*two == &t[2]);
  CHECK(ids(list) == (std::vector<int>{0, 1, 2, 3}));
  CHECK(list.size() == 4);

  // erase_after returns the element after the erased one.
  id_list::iterator after = list.erase_after(list.begin());
  CHECK(&*after == &t[2]);
  CHECK(ids(list) == (std::vector<int>{0, 2, 3}));
  CHECK(!hook_linked<id_list>(t[1]));
  // Erasing the last element returns end().
  CHECK(list.erase_after(std::next(list.begin())) == list.end());
  CHECK(ids(list) == (std::vector<int>{0, 2}));
  CHECK(list.size() == 2);
}

void erase_by_reference() {
  timer t[4] = {timer(0), timer(1), timer(2), timer(3)};
  id_list list;
  for (int i = 3; i >= 0; --i) {
    list.push_front(t[i]);
  }
  list.erase(t[2]);  // middle
  CHECK(ids(list) == (std::vector<int>{0, 1, 3}));
  list.erase(t[0]);  // first
  CHECK(ids(list) == (std::vector<int>{1, 3}));
  list.erase(t[3]);  // last
  CHECK(ids(list) == (std::vector<int>{1}));
  CHECK(list.size() == 1);
  CHECK(!hook_linked<id_list>(t[0]) && !hook_linked<id_list>(t[2]) && !hook_linked<id_list>(t[3]));
  list.erase(t[1]);
  CHECK(list.empty());
}

void clear_and_destructor_unlink() {
  timer a(1);
  timer b(2);
  {
    id_list list;
    list.push_front(a);
    list.push_front(b);
    list.clear();
    CHECK(list.empty() && list.size() == 0);
    CHECK(!hook_linked<id_list>(a) && !hook_linked<id_list>(b));
    list.push_front(a);
    list.push_front(b);
  }
  // The list's destructor unlinked both, so they can be destroyed (at the end
  // of this function) and linked again.
  CHECK(!hook_linked<id_list>(a) && !hook_linked<id_list>(b));
  id_list again;
  again.push_front(a);
  CHECK(again.size() == 1);
  again.clear();
}

void two_lists_through_two_hooks() {
  timer a(1);
  timer b(2);
  id_list by_id_list;
  age_list by_age_list;
  by_id_list.push_front(b);
  by_id_list.push_front(a);
  by_age_list.push_front(a);
  by_age_list.push_front(b);
  CHECK(ids(by_id_list) == (std::vector<int>{1, 2}));
  CHECK(ids(by_age_list) == (std::vector<int>{2, 1}));
  by_id_list.erase(a);
  CHECK(!hook_linked<id_list>(a) && hook_linked<age_list>(a));
  CHECK(ids(by_age_list) == (std::vector<int>{2, 1}));
  by_id_list.clear();
  by_age_list.clear();
}

void copy_is_unlinked() {
  timer a(7);
  id_list list;
  list.push_front(a);
  timer copy(a);  // a copy is a new object: the original's place in the list is not its
  CHECK(copy.id == 7);
  CHECK(!hook_linked<id_list>(copy));
  timer other(8);
  other = a;  // assignment copies the value and keeps other's own (unlinked) state
  CHECK(other.id == 7);
  CHECK(!hook_linked<id_list>(other));
  a = timer(9);  // and keeps a's linked state
  CHECK(hook_linked<id_list>(a));
  CHECK(&list.front() == &a);
  timer moved(std::move(a));  // moving is copying, for the hook
  CHECK(moved.id == 9);
  CHECK(!hook_linked<id_list>(moved) && hook_linked<id_list>(a));
  other = std::move(moved);
  CHECK(other.id == 9 && !hook_linked<id_list>(other));
  list.clear();
}

void const_access() {
  timer a(1);
  timer b(2);
  id_list list;
  list.push_front(b);
  list.push_front(a);
  const id_list& view = list;
  CHECK(view.front().id == 1);
  CHECK(ids(view) == (std::vector<int>{1, 2}));
  id_list::const_iterator it = view.cbefore_begin();
  ++it;
  CHECK(it->id == 1);
  id_list::const_iterator old = it++;
  CHECK(old->id == 1 && it->id == 2);
  list.clear();
}

void static_list_starts_empty() {
  CHECK(g_static_list.empty());
  plain p;
  g_static_list.push_front(p);
  CHECK(g_static_list.size() == 1);
  g_static_list.clear();
}

// ---- misuse: each traps -----------------------------------------------------

// Destroyed while linked: the list would keep a pointer to a dead object.
void destroying_a_linked_element_traps() {
  alignas(timer) unsigned char storage[sizeof(timer)];
  timer* t = ::new (static_cast<void*>(storage)) timer(1);
  id_list list;
  list.push_front(*t);
  CHECK(trips([&] { t->~timer(); }));
  // The trap left before the hook's destructor finished; the object is not
  // used again, and clear() only resets the link fields in its storage.
  list.clear();

  timer* u = ::new (static_cast<void*>(storage)) timer(2);
  CHECK(!trips([&] { u->~timer(); }));  // unlinked: destroyed quietly
}

// Linked twice: into another list, or into the same list again.
void linking_a_linked_element_traps() {
  timer a(1);
  timer b(2);
  id_list first;
  id_list second;
  first.push_front(b);
  first.push_front(a);
  CHECK(trips([&] { second.push_front(a); }));
  CHECK(trips([&] { first.push_front(a); }));
  CHECK(trips([&] { first.insert_after(first.begin(), b); }));
  CHECK(trips([&] { second.insert_after(second.before_begin(), b); }));
  CHECK(second.empty());
  CHECK(ids(first) == (std::vector<int>{1, 2}));
  first.clear();
}

// Erased through a list it does not belong to, by reference or by position.
void erasing_through_another_list_traps() {
  timer a(1);
  timer b(2);
  timer loose(3);
  id_list first;
  id_list second;
  first.push_front(b);
  first.push_front(a);
  CHECK(trips([&] { second.erase(a); }));
  CHECK(trips([&] { first.erase(loose); }));  // in no list at all
  CHECK(trips([&] { second.erase_after(first.before_begin()); }));
  CHECK(trips([&] { second.erase_after(first.begin()); }));
  CHECK(trips([&] { second.insert_after(first.begin(), loose); }));  // a position in another list
  CHECK(ids(first) == (std::vector<int>{1, 2}));
  CHECK(first.size() == 2 && second.empty());
  first.clear();
}

void past_the_end_traps() {
  timer a(1);
  timer loose(2);
  id_list list;
  CHECK(trips([&] { list.pop_front(); }));
  CHECK(trips([&] { list.erase_after(list.before_begin()); }));
  list.push_front(a);
  CHECK(trips([&] { list.erase_after(list.begin()); }));  // nothing after the last
  CHECK(trips([&] { list.insert_after(list.end(), loose); }));
  CHECK(list.size() == 1);
  list.clear();
  if (metl_test::asserts_active) {
    CHECK(trips([&] { (void)list.front(); }));
    const id_list& view = list;
    CHECK(trips([&] { (void)view.front(); }));
  }
}

}  // namespace

int main() {
  empty_list();
  push_and_pop_front();
  insert_after_and_erase_after();
  erase_by_reference();
  clear_and_destructor_unlink();
  two_lists_through_two_hooks();
  copy_is_unlinked();
  const_access();
  static_list_starts_empty();
  destroying_a_linked_element_traps();
  linking_a_linked_element_traps();
  erasing_through_another_list_traps();
  past_the_end_traps();
  return metl_test::exit_code();
}
