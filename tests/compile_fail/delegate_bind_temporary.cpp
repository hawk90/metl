// EXPECT-ERROR: delegate::bind cannot take a temporary
//
// delegate::bind for a const member function took `const T&`, which binds a
// temporary too; the delegate then called through a destroyed object. Only
// clang warned (METL_LIFETIME_BOUND). The rvalue overload now refuses it.

#include <metl/delegate.hpp>

struct sensor {
  int value = 3;
  int read() const { return value; }
};

inline int control() {
  const sensor s;
  auto d = metl::delegate<int()>::bind<sensor, &sensor::read>(s);
  return d();
}

#ifdef METL_COMPILE_FAIL
inline int binds_a_temporary() {
  auto d = metl::delegate<int()>::bind<sensor, &sensor::read>(sensor{});
  return d();
}
#endif
