// Property: a namespace-scope object is constant-initialized (#389).
//
//   constant_initialized   `METL_CONST_INIT static T object;` compiles, and the
//                          object is empty and usable at run time.
//
// A dynamically initialized global is built at startup, in link order, so
// anything that runs first -- an interrupt enabled early, another translation
// unit's static initializer -- sees it before its constructor ran (#263, #285).
// Each adapter states its level (`constant_init_level`): always, since C++20,
// or never with the reason (`constant_init_refused`). A type that loses its
// constant initialization then fails to compile here.
//
// METL_CONST_INIT can only check where the compiler enforces it: `constinit`
// (C++20), or Clang's require_constant_initialization in C++17. Elsewhere --
// GCC or MSVC in C++17 -- the property says it was not checked rather than
// pass on nothing.

#include "conformance.hpp"

#include <metl/attributes.hpp>

namespace {

using conformance::constant_init;
using conformance::probe;

#if defined(__cpp_constinit) && __cpp_constinit >= 201907L
constexpr bool enforced = true;
constexpr bool cxx20 = true;
#elif METL_HAS_CPP_ATTRIBUTE(clang::require_constant_initialization)
constexpr bool enforced = true;
constexpr bool cxx20 = false;
#else
constexpr bool enforced = false;
constexpr bool cxx20 = false;
#endif

template <typename A, typename = void>
struct refuses_constant_init : std::false_type {};
template <typename A>
struct refuses_constant_init<A, std::void_t<decltype(A::constant_init_refused)>> : std::true_type {};

template <typename Adapter>
constexpr bool checkable =
    enforced && (Adapter::constant_init_level == constant_init::always ||
                 (Adapter::constant_init_level == constant_init::since_cxx20 && cxx20));

// Instantiated only for the adapters checked in this build.
template <typename Adapter>
METL_CONST_INIT typename Adapter::type constant_object;

struct constant_initialized {
  template <typename Adapter>
  bool operator()(Adapter) const {
    auto& c = constant_object<Adapter>;
    const std::size_t empty = Adapter::held(c);
    const bool added = Adapter::try_add(c, 1);
    const std::size_t one = Adapter::held(c);
    Adapter::clear(c);
    const std::size_t cleared = Adapter::held(c);
    probe::live() = 0;
    const bool met = empty == 0 && added && one == 1 && cleared == 0;
    if (!met) {
      std::fprintf(stderr,
                   "  %s: the constant-initialized object held %zu, added %d (then %zu), %zu after clear\n",
                   Adapter::name,
                   empty,
                   added,
                   one,
                   cleared);
    }
    return met;
  }
};

template <typename Adapter>
void run_for() {
  static_assert(
      (Adapter::constant_init_level == constant_init::never) == refuses_constant_init<Adapter>::value,
      "an adapter that is never constant-initialized says why (constant_init_refused), and only then");
  if constexpr (Adapter::constant_init_level == constant_init::never) {
    std::printf(
        "%s: constant_initialized: not applicable: %s\n", Adapter::name, Adapter::constant_init_refused);
  } else if constexpr (!checkable<Adapter>) {
    std::printf("%s: constant_initialized: not checked: %s\n",
                Adapter::name,
                enforced ? "promised since C++20; this build is C++17"
                         : "this compiler does not enforce METL_CONST_INIT in C++17");
  } else {
    conformance::run_one<Adapter>("constant_initialized", nullptr, constant_initialized{});
  }
}

template <typename... Adapters>
void run_all(std::tuple<Adapters...>*) {
  (run_for<Adapters>(), ...);
}

}  // namespace

int main() {
  run_all(static_cast<conformance::registry*>(nullptr));
  return metl_test::exit_code();
}
