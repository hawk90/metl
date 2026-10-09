// Assignment semantics of optional and variant that std users rely on.
//
//   * optional<bool> from another optional: bool is constructible from
//     optional<U> through its explicit operator bool, so the source's
//     has_value() became the stored value (an empty optional<int> gave an
//     engaged optional<bool> holding false). Refused now.
//   * variant assigned the alternative it already holds: it destroyed and
//     reconstructed instead of calling T::operator=, which lost the count an
//     intrusive_ref_counter keeps across assignment.
//   * variant::emplace of a non-movable alternative from an argument inside the
//     variant: built from an object reset() had just destroyed. Refused now.

#include "metl_check.hpp"

#include <csetjmp>
#include <cstddef>
#include <type_traits>

#include <metl/assert.hpp>
#include <metl/in_place.hpp>
#include <metl/intrusive_ptr.hpp>
#include <metl/optional.hpp>
#include <metl/variant.hpp>

namespace {

// --- optional<bool> ----------------------------------------------------------

static_assert(!std::is_constructible_v<metl::optional<bool>, metl::optional<int>>,
              "optional<bool> must not take an optional's has_value() as its value");
static_assert(!std::is_constructible_v<metl::optional<bool>, const metl::optional<int>&>, "");
static_assert(!std::is_assignable_v<metl::optional<bool>&, metl::optional<int>>, "");
static_assert(std::is_constructible_v<metl::optional<bool>, bool>, "a bool still converts");
static_assert(std::is_constructible_v<metl::optional<bool>, int>, "so does an int");
static_assert(std::is_constructible_v<metl::optional<int>, metl::optional<int>>, "copy is unaffected");

// --- variant assignment -------------------------------------------------------

struct counts {
  int copy_construct = 0;
  int move_construct = 0;
  int copy_assign = 0;
  int move_assign = 0;
  int destroy = 0;
};
counts g_counts;

struct counted {
  int value = 0;
  explicit counted(int v) : value(v) {}
  counted(const counted& other) : value(other.value) { ++g_counts.copy_construct; }
  counted(counted&& other) noexcept : value(other.value) { ++g_counts.move_construct; }
  counted& operator=(const counted& other) {
    value = other.value;
    ++g_counts.copy_assign;
    return *this;
  }
  counted& operator=(counted&& other) noexcept {
    value = other.value;
    ++g_counts.move_assign;
    return *this;
  }
  ~counted() { ++g_counts.destroy; }
};

void same_alternative_assigns_in_place() {
  metl::variant<int, counted> a{metl::in_place_type<counted>, 1};
  metl::variant<int, counted> b{metl::in_place_type<counted>, 2};
  g_counts = {};
  a = b;
  CHECK_EQ(g_counts.copy_assign, 1);
  CHECK_EQ(g_counts.copy_construct, 0);
  CHECK_EQ(g_counts.destroy, 0);
  CHECK_DEREF_EQ(&metl::get<counted>(a).value, 2);

  g_counts = {};
  a = metl::variant<int, counted>{metl::in_place_type<counted>, 3};
  CHECK_EQ(g_counts.move_assign, 1);
  CHECK_EQ(g_counts.move_construct, 0);
  CHECK_DEREF_EQ(&metl::get<counted>(a).value, 3);
}

struct node final : metl::intrusive_ref_counter<node> {
  int value;
  explicit node(int v) : value(v) {}
};

void reference_count_survives_assignment() {
  metl::variant<int, node> slot{metl::in_place_type<node>, 1};
  const metl::variant<int, node> other{metl::in_place_type<node>, 2};
  metl::intrusive_ptr<node> holder(metl::get_if<node>(&slot), metl::retain_ref);
  CHECK_EQ(holder->use_count(), std::size_t{1});
  slot = other;
  CHECK_EQ(holder->use_count(), std::size_t{1});  // the object was assigned, not replaced
  CHECK_EQ(holder->value, 2);
  holder.reset();  // used to release a zero count
}

struct not_assignable {
  const int value;
  explicit not_assignable(int v) : value(v) {}
};

void unassignable_alternative_still_works() {
  metl::variant<int, not_assignable> a{metl::in_place_type<not_assignable>, 1};
  const metl::variant<int, not_assignable> b{metl::in_place_type<not_assignable>, 2};
  a = b;
  CHECK_EQ(metl::get<not_assignable>(a).value, 2);
}

// --- variant::emplace aliasing -------------------------------------------------

std::jmp_buf g_jump;
bool g_fired = false;

void capture(const char* /*expr*/, const char* /*file*/, int /*line*/) noexcept {
  g_fired = true;
  std::longjmp(g_jump, 1);
}

struct pinned {
  int value;
  explicit pinned(const counted& source) : value(source.value) {}
  pinned(const pinned&) = delete;
  pinned& operator=(const pinned&) = delete;
};

void emplace_of_unmovable_from_own_alternative_is_refused() {
  metl::set_assert_handler(&capture);
  metl::variant<counted, pinned> v{metl::in_place_type<counted>, 7};
  g_fired = false;
  if (setjmp(g_jump) == 0) {
    v.emplace<pinned>(metl::get<counted>(v));
  }
  CHECK(g_fired);
  CHECK(metl::holds_alternative<counted>(v));  // untouched: refused before reset()

  const counted outside{9};
  v.emplace<pinned>(outside);  // an argument outside the variant is fine
  CHECK_EQ(metl::get<pinned>(v).value, 9);
}

}  // namespace

int main() {
  same_alternative_assigns_in_place();
  reference_count_survives_assignment();
  unassignable_alternative_still_works();
  emplace_of_unmovable_from_own_alternative_is_refused();
  return metl_test::exit_code();
}
