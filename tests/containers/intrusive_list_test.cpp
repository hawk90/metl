// metl::intrusive_list (#337): every operation against the expected
// element order in both directions, an element in two lists through two hooks,
// and the misuses the checked hook exists for, each of which must trap:
// destroying a linked element, linking it a second time, erasing it through a
// list it does not belong to, and erasing end().

#include "assert_trip.hpp"
#include "metl_check.hpp"

#include <cstddef>
#include <cstdio>
#include <iterator>
#include <list>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#include <metl/attributes.hpp>
#include <metl/intrusive_list.hpp>

namespace {

using metl_test::trips;

struct all_timers {};
struct armed {};

// Two hooks: one timer can be in the all-timers list and the armed list at once.
struct timer : metl::list_hook<all_timers>, metl::list_hook<armed> {
  explicit timer(int id_ = 0) : id(id_) {}
  int id;
};

using all_list = metl::intrusive_list<timer, all_timers>;
using armed_list = metl::intrusive_list<timer, armed>;

struct plain : metl::list_hook<> {
  int value = 0;
};
using plain_list = metl::intrusive_list<plain>;

// ---- compile time -----------------------------------------------------------

// The measured cost of the check: the hook holds its two neighbours and its
// owner, one pointer more than an unchecked hook.
static_assert(sizeof(metl::list_hook<>) == 3 * sizeof(void*), "next, prev and owner");
static_assert(sizeof(plain_list) == 3 * sizeof(void*) + sizeof(std::size_t), "head link and size");

static_assert(std::is_same<std::iterator_traits<all_list::iterator>::iterator_category,
                           std::bidirectional_iterator_tag>::value,
              "a bidirectional iterator");
static_assert(std::is_same<decltype(*std::declval<all_list::const_iterator>()), const timer&>::value,
              "a const_iterator yields const elements");
static_assert(std::is_convertible<all_list::iterator, all_list::const_iterator>::value,
              "iterator -> const_iterator");
static_assert(!std::is_copy_constructible<all_list>::value && !std::is_move_constructible<all_list>::value,
              "elements and the head record the list's address, so a list stays where it is");
static_assert(std::is_nothrow_default_constructible<metl::list_hook<>>::value, "noexcept hook");

// A namespace-scope list is constant-initialized even though its head points at
// itself: no constructor runs before main, so a static initializer in another
// translation unit can link into it.
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

// The same walk backwards from end(), so a broken prev link shows.
template <typename List>
std::vector<int> ids_reversed(const List& list) {
  std::vector<int> out;
  for (auto it = list.end(); it != list.begin();) {
    --it;
    out.push_back(it->id);
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
  all_list list;
  CHECK(list.empty());
  CHECK(list.size() == 0);
  CHECK(list.begin() == list.end());
  CHECK(list.cbegin() == list.cend());
}

void push_and_pop_at_both_ends() {
  timer a(1);
  timer b(2);
  timer c(3);
  timer d(4);
  all_list list;
  list.push_back(b);
  list.push_front(a);
  list.push_back(c);
  list.push_back(d);
  CHECK(ids(list) == (std::vector<int>{1, 2, 3, 4}));
  CHECK(ids_reversed(list) == (std::vector<int>{4, 3, 2, 1}));
  CHECK(list.size() == 4);
  CHECK(&list.front() == &a && &list.back() == &d);

  list.pop_front();
  list.pop_back();
  CHECK(ids(list) == (std::vector<int>{2, 3}));
  CHECK(ids_reversed(list) == (std::vector<int>{3, 2}));
  CHECK(!hook_linked<all_list>(a) && !hook_linked<all_list>(d));
  list.pop_back();
  list.pop_front();
  CHECK(list.empty() && list.size() == 0);
  CHECK(list.begin() == list.end());
}

void insert_and_erase_at_a_position() {
  timer t[4] = {timer(0), timer(1), timer(2), timer(3)};
  all_list list;
  // insert puts the element before pos, so end() appends.
  all_list::iterator it = list.insert(list.end(), t[0]);
  CHECK(&*it == &t[0]);
  list.insert(list.end(), t[3]);
  list.insert(std::next(list.begin()), t[1]);  // before 3
  all_list::iterator two = list.insert(std::prev(list.end()), t[2]);
  CHECK(&*two == &t[2]);
  CHECK(ids(list) == (std::vector<int>{0, 1, 2, 3}));
  CHECK(ids_reversed(list) == (std::vector<int>{3, 2, 1, 0}));

  // erase returns the element after the erased one.
  all_list::iterator after = list.erase(std::next(list.begin()));
  CHECK(&*after == &t[2]);
  CHECK(!hook_linked<all_list>(t[1]));
  CHECK(list.erase(std::prev(list.end())) == list.end());  // the last
  CHECK(ids(list) == (std::vector<int>{0, 2}));
  CHECK(ids_reversed(list) == (std::vector<int>{2, 0}));
  CHECK(list.size() == 2);
  list.clear();
}

// The reason for a doubly linked list: an element unlinks itself in O(1),
// wherever it is, as a timer does when it is cancelled.
void erase_by_reference() {
  timer t[4] = {timer(0), timer(1), timer(2), timer(3)};
  all_list list;
  for (timer& x : t) {
    list.push_back(x);
  }
  list.erase(t[2]);  // middle
  CHECK(ids(list) == (std::vector<int>{0, 1, 3}));
  list.erase(t[0]);  // first
  CHECK(ids(list) == (std::vector<int>{1, 3}));
  list.erase(t[3]);  // last
  CHECK(ids(list) == (std::vector<int>{1}));
  CHECK(ids_reversed(list) == (std::vector<int>{1}));
  CHECK(list.size() == 1);
  CHECK(!hook_linked<all_list>(t[0]) && !hook_linked<all_list>(t[2]) && !hook_linked<all_list>(t[3]));
  list.erase(t[1]);
  CHECK(list.empty());
  CHECK(list.begin() == list.end());
}

void clear_and_destructor_unlink() {
  timer a(1);
  timer b(2);
  {
    all_list list;
    list.push_back(a);
    list.push_back(b);
    list.clear();
    CHECK(list.empty() && list.size() == 0);
    CHECK(!hook_linked<all_list>(a) && !hook_linked<all_list>(b));
    list.push_back(a);
    list.push_back(b);
  }
  // The list's destructor unlinked both, so they can be destroyed (at the end
  // of this function) and linked again.
  CHECK(!hook_linked<all_list>(a) && !hook_linked<all_list>(b));
  all_list again;
  again.push_back(a);
  CHECK(again.size() == 1);
  again.clear();
}

// A timer in the all-timers list and the armed list at once; cancelling it
// removes it from the armed list only.
void two_lists_through_two_hooks() {
  timer a(1);
  timer b(2);
  all_list all;
  armed_list armed_timers;
  all.push_back(a);
  all.push_back(b);
  armed_timers.push_back(b);
  armed_timers.push_back(a);
  CHECK(ids(all) == (std::vector<int>{1, 2}));
  CHECK(ids(armed_timers) == (std::vector<int>{2, 1}));
  armed_timers.erase(a);
  CHECK(hook_linked<all_list>(a) && !hook_linked<armed_list>(a));
  CHECK(ids(all) == (std::vector<int>{1, 2}));
  CHECK(ids(armed_timers) == (std::vector<int>{2}));
  all.clear();
  armed_timers.clear();
}

void copy_is_unlinked() {
  timer a(7);
  all_list list;
  list.push_back(a);
  timer copy(a);  // a copy is a new object: the original's place in the list is not its
  CHECK(copy.id == 7);
  CHECK(!hook_linked<all_list>(copy));
  timer other(8);
  other = a;  // assignment copies the value and keeps other's own (unlinked) state
  CHECK(other.id == 7);
  CHECK(!hook_linked<all_list>(other));
  a = timer(9);  // and keeps a's linked state
  CHECK(hook_linked<all_list>(a));
  CHECK(&list.front() == &a);
  timer moved(std::move(a));  // moving is copying, for the hook
  CHECK(moved.id == 9);
  CHECK(!hook_linked<all_list>(moved) && hook_linked<all_list>(a));
  other = std::move(moved);
  CHECK(other.id == 9 && !hook_linked<all_list>(other));
  list.clear();
}

void const_access() {
  timer a(1);
  timer b(2);
  all_list list;
  list.push_back(a);
  list.push_back(b);
  const all_list& view = list;
  CHECK(view.front().id == 1 && view.back().id == 2);
  CHECK(ids(view) == (std::vector<int>{1, 2}));
  CHECK(ids_reversed(view) == (std::vector<int>{2, 1}));
  all_list::const_iterator it = view.cbegin();
  all_list::const_iterator old = it++;
  CHECK(old->id == 1 && it->id == 2);
  old = it--;
  CHECK(old->id == 2 && it->id == 1);
  list.clear();
}

void static_list_starts_empty() {
  CHECK(g_static_list.empty());
  CHECK(g_static_list.begin() == g_static_list.end());
  plain p;
  g_static_list.push_back(p);
  CHECK(g_static_list.size() == 1);
  g_static_list.clear();
}

// ---- misuse: each traps -----------------------------------------------------

// Destroyed while linked: the list would keep a pointer to a dead object.
void destroying_a_linked_element_traps() {
  alignas(timer) unsigned char storage[sizeof(timer)];
  timer* t = ::new (static_cast<void*>(storage)) timer(1);
  all_list list;
  list.push_back(*t);
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
  all_list first;
  all_list second;
  first.push_back(a);
  first.push_back(b);
  CHECK(trips([&] { second.push_back(a); }));
  CHECK(trips([&] { second.push_front(a); }));
  CHECK(trips([&] { first.push_back(a); }));
  CHECK(trips([&] { first.insert(first.begin(), b); }));
  CHECK(second.empty());
  CHECK(ids(first) == (std::vector<int>{1, 2}));
  CHECK(ids_reversed(first) == (std::vector<int>{2, 1}));
  first.clear();
}

// Erased through a list it does not belong to, by reference or by position,
// or inserted at a position in another list.
void erasing_through_another_list_traps() {
  timer a(1);
  timer b(2);
  timer loose(3);
  all_list first;
  all_list second;
  first.push_back(a);
  first.push_back(b);
  CHECK(trips([&] { second.erase(a); }));
  CHECK(trips([&] { first.erase(loose); }));  // in no list at all
  CHECK(trips([&] { second.erase(first.begin()); }));
  CHECK(trips([&] { second.insert(first.begin(), loose); }));
  CHECK(trips([&] { second.insert(first.end(), loose); }));           // another list's end()
  CHECK(trips([&] { second.insert(all_list::iterator(), loose); }));  // no list at all
  CHECK(ids(first) == (std::vector<int>{1, 2}));
  CHECK(first.size() == 2 && second.empty());
  first.clear();
}

// end() is a position in the list but not an element: inserting before it
// appends, and erasing it would unlink the head.
void erasing_end_traps() {
  timer a(1);
  all_list list;
  CHECK(trips([&] { list.pop_front(); }));
  CHECK(trips([&] { list.pop_back(); }));
  CHECK(trips([&] { list.erase(list.end()); }));
  list.push_back(a);
  CHECK(trips([&] { list.erase(list.end()); }));
  CHECK(list.size() == 1);
  CHECK(ids(list) == (std::vector<int>{1}));
  list.clear();
  if (metl_test::asserts_active) {
    CHECK(trips([&] { (void)list.front(); }));
    CHECK(trips([&] { (void)list.back(); }));
    const all_list& view = list;
    CHECK(trips([&] { (void)view.front(); }));
    CHECK(trips([&] { (void)view.back(); }));
  }
}

// splice moves one element without unlinking it: within a list, as a round's
// cursor does, or into another list, which then owns it.
void splice_moves_one_element() {
  timer a(1);
  timer b(2);
  timer c(3);
  timer d(4);
  all_list first;
  all_list second;
  first.push_back(a);
  first.push_back(b);
  first.push_back(c);

  auto it_a = first.begin();
  first.splice(first.end(), first, it_a);  // a to the back
  CHECK(ids(first) == (std::vector<int>{2, 3, 1}));
  CHECK(ids_reversed(first) == (std::vector<int>{1, 3, 2}));
  CHECK(&*it_a == &a);  // the iterator still names a
  CHECK(first.size() == 3);

  first.splice(first.begin(), first, it_a);  // and back to the front
  CHECK(ids(first) == (std::vector<int>{1, 2, 3}));
  CHECK(ids_reversed(first) == (std::vector<int>{3, 2, 1}));

  // Before itself, or before its own successor: already there.
  first.splice(it_a, first, it_a);
  auto after_a = it_a;
  ++after_a;
  first.splice(after_a, first, it_a);
  CHECK(ids(first) == (std::vector<int>{1, 2, 3}));
  CHECK(ids_reversed(first) == (std::vector<int>{3, 2, 1}));

  // Into another list: sizes move with it, and so does ownership.
  second.push_back(d);
  auto it_b = after_a;  // b
  second.splice(second.begin(), first, it_b);
  CHECK(ids(first) == (std::vector<int>{1, 3}));
  CHECK(ids_reversed(first) == (std::vector<int>{3, 1}));
  CHECK(ids(second) == (std::vector<int>{2, 4}));
  CHECK(ids_reversed(second) == (std::vector<int>{4, 2}));
  CHECK(first.size() == 2);
  CHECK(second.size() == 2);
  CHECK(trips([&] { first.erase(b); }));  // b is second's now
  second.erase(b);
  CHECK(!hook_linked<all_list>(b));
  first.clear();
  second.clear();
}

// Each position names the list it must be in.
void splicing_through_the_wrong_list_traps() {
  timer a(1);
  timer b(2);
  timer loose(9);
  all_list first;
  all_list second;
  first.push_back(a);
  second.push_back(b);
  CHECK(trips([&] { first.splice(second.begin(), first, first.begin()); }));        // pos elsewhere
  CHECK(trips([&] { first.splice(first.end(), second, first.begin()); }));          // it not in other
  CHECK(trips([&] { first.splice(first.end(), first, first.end()); }));             // end(): no element
  CHECK(trips([&] { first.splice(all_list::iterator(), first, first.begin()); }));  // no position
  CHECK(trips([&] { first.splice(first.end(), second, all_list::iterator()); }));   // no element
  CHECK(!hook_linked<all_list>(loose));
  CHECK(ids(first) == (std::vector<int>{1}));
  CHECK(ids(second) == (std::vector<int>{2}));
  CHECK(first.size() == 1 && second.size() == 1);
  first.clear();
  second.clear();
}

// ---- agreement with std::list -------------------------------------------------

// A fixed pseudo-random sequence of every operation the list shares with
// std::list, applied to both: elements are linked here and copied there, so
// the two must hold the same ids in the same order after each step, forwards
// and backwards, and return the same element from insert and erase. This is
// the std-parity property of the conformance suite (#391), which cannot run
// over the intrusive lists through its adapters: they construct and destroy
// the elements a type holds, and these lists hold none.
void agrees_with_std_list() {
  constexpr int kPool = 12;
  std::vector<timer> pool;
  pool.reserve(kPool);
  for (int i = 0; i < kPool; ++i) {
    pool.emplace_back(i);
  }
  all_list list;
  std::list<int> ref;
  unsigned state = 12345u;
  auto next = [&state](unsigned bound) {
    state = state * 1103515245u + 12345u;
    return (state >> 16) % bound;
  };
  // The position in each sequence `k` steps from the front.
  auto at = [&](std::size_t k) {
    auto a = list.begin();
    auto b = ref.begin();
    std::advance(a, static_cast<std::ptrdiff_t>(k));
    std::advance(b, static_cast<std::ptrdiff_t>(k));
    return std::make_pair(a, b);
  };
  auto loose = [&]() -> timer* {
    for (timer& t : pool) {
      if (!hook_linked<all_list>(t)) {
        return &t;
      }
    }
    return nullptr;
  };
  bool agreed = true;
  for (int step = 0; step < 400 && agreed; ++step) {
    const std::size_t n = ref.size();
    timer* const free_one = loose();
    switch (next(9)) {
      case 0:
        if (free_one != nullptr) {
          list.push_front(*free_one);
          ref.push_front(free_one->id);
        }
        break;
      case 1:
        if (free_one != nullptr) {
          list.push_back(*free_one);
          ref.push_back(free_one->id);
        }
        break;
      case 2:
        if (n != 0) {
          agreed = list.front().id == ref.front() && list.back().id == ref.back();
          list.pop_front();
          ref.pop_front();
        }
        break;
      case 3:
        if (n != 0) {
          list.pop_back();
          ref.pop_back();
        }
        break;
      case 4:
        if (free_one != nullptr) {
          auto pos = at(next(static_cast<unsigned>(n + 1)));
          const auto mine = list.insert(pos.first, *free_one);
          const auto theirs = ref.insert(pos.second, free_one->id);
          agreed = mine->id == *theirs;
        }
        break;
      case 5:
        if (n != 0) {
          auto pos = at(next(static_cast<unsigned>(n)));
          const auto mine = list.erase(pos.first);
          const auto theirs = ref.erase(pos.second);
          agreed =
              (mine == list.end()) == (theirs == ref.end()) && (mine == list.end() || mine->id == *theirs);
        }
        break;
      case 6:
        if (n != 0) {
          // erase(t) has no std::list member; std's equivalent is erasing
          // the element's own position.
          auto pos = at(next(static_cast<unsigned>(n)));
          list.erase(*pos.first);
          ref.erase(pos.second);
        }
        break;
      case 7:
        if (n != 0) {
          auto from = at(next(static_cast<unsigned>(n)));
          auto to = at(next(static_cast<unsigned>(n + 1)));
          list.splice(to.first, list, from.first);
          ref.splice(to.second, ref, from.second);
        }
        break;
      default:
        if (next(8) == 0) {
          list.clear();
          ref.clear();
        }
        break;
    }
    const std::vector<int> want(ref.begin(), ref.end());
    const std::vector<int> want_reversed(ref.rbegin(), ref.rend());
    agreed = agreed && list.size() == ref.size() && list.empty() == ref.empty() && ids(list) == want &&
             ids_reversed(list) == want_reversed;
    if (!agreed) {
      std::fprintf(stderr, "intrusive_list and std::list disagree after step %d\n", step);
    }
  }
  CHECK(agreed);
  list.clear();
}

}  // namespace

int main() {
  empty_list();
  push_and_pop_at_both_ends();
  insert_and_erase_at_a_position();
  erase_by_reference();
  clear_and_destructor_unlink();
  two_lists_through_two_hooks();
  copy_is_unlinked();
  const_access();
  static_list_starts_empty();
  destroying_a_linked_element_traps();
  linking_a_linked_element_traps();
  erasing_through_another_list_traps();
  erasing_end_traps();
  splice_moves_one_element();
  splicing_through_the_wrong_list_traps();
  agrees_with_std_list();
  return metl_test::exit_code();
}
