// EXPECT-ERROR: event_dispatcher cannot take an rvalue-reference parameter
//
// One event goes to every listener, so no listener may move from it.
// dispatch() used to fail deep inside the header instead.

#include <metl/event_dispatcher.hpp>

inline void control() {
  metl::event_dispatcher<void(const int&), 2> dispatcher;
  dispatcher.dispatch(5);
}

#ifdef METL_COMPILE_FAIL
inline void takes_an_rvalue_reference() {
  metl::event_dispatcher<void(int&&), 2> dispatcher;
  dispatcher.dispatch(5);
}
#endif
