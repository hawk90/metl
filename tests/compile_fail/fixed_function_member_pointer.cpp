// EXPECT-ERROR: fixed_function / fixed_any_invocable cannot store a member pointer
//
// The wrappers call their target as `f(args...)`, which a member pointer
// cannot do. They used to accept one anyway (is_invocable_r counts member
// pointers), so trait-based overload selection picked them and construction
// then failed deep inside the header. Member pointers are now refused up front.

#include <metl/fixed_function.hpp>

struct widget {
  int value = 1;
  int get() { return value; }
};

static_assert(!std::is_constructible_v<metl::fixed_function<int(widget&)>, int (widget::*)()>,
              "the constructors no longer claim to take a member pointer");

inline int control() {
  metl::fixed_function<int(widget&)> f([](widget& w) { return w.get(); });
  widget w;
  return f(w);
}

#ifdef METL_COMPILE_FAIL
inline bool stores_a_member_pointer() {
  metl::fixed_function<int(widget&)> f;
  return f.try_assign(&widget::get);
}
#endif
