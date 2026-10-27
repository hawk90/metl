// EXPECT-ERROR: T must derive publicly from list_hook<Tag>
//
// The list reaches an element through its hook base, so a T without that hook,
// or with a hook of another Tag, has nothing to link through. Without the
// assert the error came from a static_cast deep inside insert.

#include <metl/intrusive_list.hpp>

struct other_tag {};

struct hooked : metl::list_hook<> {};
metl::intrusive_list<hooked> control;  // control instantiation

#ifdef METL_COMPILE_FAIL
struct hooked_for_other : metl::list_hook<other_tag> {};
metl::intrusive_list<hooked_for_other> wrong_tag;
#endif
