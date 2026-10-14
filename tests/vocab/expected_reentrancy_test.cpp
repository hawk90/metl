// A member's destructor that reaches back into its expected (#374).
//
// Switching between value and error destroyed the old member while
// has_value() still named it, so a destructor that changed the same expected
// destroyed the old member a second time. Now, while a member is destroyed,
// the expected reads as holding no value, and a change fails METL_HARDEN.
//
// Every path that destroys a member is checked: emplace and emplace_error
// across and within a state, assignment, swap, the destructor, and
// expected<void, E>. A refused change longjmps out of the handler, which leaves
// that expected mid-switch, so those are built in storage never destroyed.

#include "metl_check.hpp"

#include <csetjmp>
#include <new>
#include <utility>

#include <metl/assert.hpp>
#include <metl/expected.hpp>
#include <metl/in_place.hpp>

namespace {

std::jmp_buf g_jump;
int g_refused = 0;

void refuse(const char* /*expr*/, const char* /*file*/, int /*line*/) noexcept {
  ++g_refused;
  std::longjmp(g_jump, 1);
}

// What a destructor saw of its owner, and whether it should try to change it.
struct watch {
  const void* owner = nullptr;
  bool (*has_value)(const void*) = nullptr;
  void (*change)(void*) = nullptr;
  int destroyed = 0;
  int saw_value = 0;
};
watch g_watch;

struct member {
  int id;
  explicit member(int i) noexcept : id(i) {}
  member(const member& other) noexcept : id(other.id) {}
  member(member&& other) noexcept : id(other.id) { other.id = 0; }
  member& operator=(const member&) = default;
  member& operator=(member&&) = default;
  ~member() {
    if (id == 0 || g_watch.owner == nullptr) {
      return;
    }
    ++g_watch.destroyed;
    if (g_watch.has_value(g_watch.owner)) {
      ++g_watch.saw_value;
    }
    if (g_watch.change != nullptr) {
      void (*change)(void*) = g_watch.change;
      g_watch.change = nullptr;  // try once
      change(const_cast<void*>(g_watch.owner));
    }
  }
};

using result = metl::expected<member, member>;
using void_result = metl::expected<void, member>;

template <typename R>
bool has_value_of(const void* p) {
  return static_cast<const R*>(p)->has_value();
}

template <typename R>
void observe(const R& owner) {
  g_watch = watch{};
  g_watch.owner = &owner;
  g_watch.has_value = &has_value_of<R>;
}

// ---- While a member is destroyed, the expected holds no value ---------------

void each_switch_hides_the_old_member() {
  {
    result e(metl::in_place, 1);
    observe(e);
    e.emplace_error(2);  // value -> error
    CHECK_EQ(g_watch.destroyed, 1);
    CHECK_EQ(g_watch.saw_value, 0);
    observe(e);
    e.emplace(3);  // error -> value
    CHECK_EQ(g_watch.destroyed, 1);
    observe(e);
    e.emplace(4);  // value -> value, in place
    CHECK_EQ(g_watch.destroyed, 1);
    CHECK_EQ(g_watch.saw_value, 0);
    observe(e);
    e = metl::unexpected<member>(member(5));  // value -> error by assignment
    CHECK_EQ(g_watch.destroyed, 1);
    CHECK_EQ(g_watch.saw_value, 0);
    g_watch = watch{};
  }
  {
    result v(metl::in_place, 1);
    result e(metl::unexpect, 2);
    observe(v);
    v.swap(e);  // v's value moves out and is destroyed in place
    CHECK_EQ(g_watch.saw_value, 0);
    CHECK(!v.has_value());
    CHECK(e.has_value());
    g_watch = watch{};
  }
  {
    alignas(result) unsigned char raw[sizeof(result)];
    result* e = ::new (static_cast<void*>(raw)) result(metl::in_place, 1);
    observe(*e);
    e->~result();
    CHECK_EQ(g_watch.destroyed, 1);
    CHECK_EQ(g_watch.saw_value, 0);
    g_watch = watch{};
  }
  {
    void_result e(metl::unexpect, 1);
    observe(e);
    e.emplace();  // error -> success
    CHECK_EQ(g_watch.destroyed, 1);
    CHECK_EQ(g_watch.saw_value, 0);  // not yet success while the error ends
    g_watch = watch{};
  }
}

// ---- A change from a destructor is refused --------------------------------------

template <typename R>
void emplace_error_on(void* p) {
  static_cast<R*>(p)->emplace_error(9);
}

template <typename R, typename Start>
bool change_is_refused(Start start) {
  alignas(R) static unsigned char raw[sizeof(R)];  // never destroyed: see the top
  R* e = ::new (static_cast<void*>(raw)) R(start());
  observe(*e);
  g_watch.change = &emplace_error_on<R>;
  g_refused = 0;
  if (setjmp(g_jump) == 0) {
    e->emplace_error(2);
  }
  const bool refused = g_refused == 1 && g_watch.destroyed == 1;
  g_watch = watch{};
  return refused;
}

void a_change_from_a_destructor_is_refused() {
  CHECK((change_is_refused<result>([] { return result(metl::in_place, 1); })));
  CHECK((change_is_refused<void_result>([] { return void_result(metl::unexpect, 1); })));
}

}  // namespace

int main() {
  const metl::assert_handler_t previous = metl::set_assert_handler(&refuse);
  each_switch_hides_the_old_member();
  a_change_from_a_destructor_is_refused();
  metl::set_assert_handler(previous);
  return metl_test::exit_code();
}
