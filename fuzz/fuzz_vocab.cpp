// libFuzzer harness for metl::optional, metl::expected and metl::variant.
//
// These three share one hazard and it is the reason they are fuzzed together:
// each holds its payload in INLINE aligned storage and changes which object
// lives there by destroying one and constructing another. Every assignment,
// every `emplace`, every `reset` is a hand-written destroy/construct pair, and
// `expected` and `variant` have to do it between DIFFERENT types.
//
// A mistake there does not look like a memory error. Destroying twice, or
// forgetting to destroy, or constructing over a live object all happen inside a
// member array that ASan considers entirely valid -- it is a live part of a live
// object. So there is nothing for a sanitizer to report, and a correctness check
// on the value would pass too, because the value the caller reads afterwards is
// usually right.
//
// What catches it is counting. The payload here tracks its own live instances,
// so the harness can assert the exact invariant those types owe:
//
//   an engaged optional holds exactly one live payload, a disengaged one holds
//   none; a variant holds exactly one live alternative; an expected holds
//   exactly one of value or error -- after EVERY operation, including the ones
//   that switch between them.
//
// Only contract-valid operations. `value()` and `get<>()` assert when the wrong
// state is active, so every unchecked accessor is guarded by the corresponding
// query first -- which is what the library tells callers to do.

#include "fuzz_helpers.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/expected.hpp>
#include <metl/optional.hpp>
#include <metl/variant.hpp>

namespace {

int g_live = 0;

/// Non-trivial on purpose: a trivially destructible payload would let a skipped
/// destructor pass unnoticed, which is the bug being hunted.
struct payload {
  std::uint32_t value;

  explicit payload(std::uint32_t v) noexcept : value(v) { ++g_live; }
  payload(const payload& other) noexcept : value(other.value) { ++g_live; }
  payload(payload&& other) noexcept : value(other.value) { ++g_live; }
  payload& operator=(const payload& other) noexcept {
    value = other.value;
    return *this;
  }
  payload& operator=(payload&& other) noexcept {
    value = other.value;
    return *this;
  }
  ~payload() { --g_live; }
};

/// A second, distinct counted type, so `expected` and `variant` switching
/// between alternatives is visible as two separate counts rather than one that
/// happens to balance.
int g_live_other = 0;

struct other_payload {
  std::uint64_t value;

  explicit other_payload(std::uint64_t v) noexcept : value(v) { ++g_live_other; }
  other_payload(const other_payload& o) noexcept : value(o.value) { ++g_live_other; }
  other_payload(other_payload&& o) noexcept : value(o.value) { ++g_live_other; }
  other_payload& operator=(const other_payload& o) noexcept {
    value = o.value;
    return *this;
  }
  other_payload& operator=(other_payload&& o) noexcept {
    value = o.value;
    return *this;
  }
  ~other_payload() { --g_live_other; }
};

void drive_optional(metl_fuzz::byte_reader& in) {
  metl::optional<payload> opt;
  metl::optional<payload> other;

  while (!in.empty()) {
    const std::uint32_t value = in.integer<std::uint32_t>();
    switch (in.byte() % 8u) {
      case 0:
        opt.emplace(value);
        break;
      case 1:
        opt.reset();
        break;
      case 2:
        opt = metl::optional<payload>(payload{value});
        break;
      case 3:
        opt = metl::nullopt;
        break;
      case 4:
        other.emplace(value);
        break;
      case 5:  // engaged <- disengaged and every other combination, which is
               // where the destroy/construct pairing has to be right
        opt = other;
        break;
      case 6:
        opt = static_cast<metl::optional<payload>&&>(other);
        break;
      default:
        opt.swap(other);
        break;
    }

    // Exactly one live payload per engaged optional. Two optionals here, so the
    // count must be the number of them that are engaged -- no more, no less.
    const int engaged = (opt.has_value() ? 1 : 0) + (other.has_value() ? 1 : 0);
    if (g_live != engaged) {
      __builtin_trap();
    }
    if (opt.has_value() && opt->value != opt.value().value) {
      __builtin_trap();
    }
  }
}

void drive_expected(metl_fuzz::byte_reader& in) {
  using result = metl::expected<payload, other_payload>;
  result r{payload{0}};

  while (!in.empty()) {
    const std::uint32_t value = in.integer<std::uint32_t>();
    switch (in.byte() % 5u) {
      case 0:
        r = result{payload{value}};
        break;
      case 1:  // value -> error and back: reinitialisation between DIFFERENT
               // types, the path with the most to get wrong
        r = result{metl::unexpected<other_payload>(other_payload{value})};
        break;
      case 2:
        r.emplace(value);
        break;
      case 3:
        if (r.has_value() && r->value != r.value().value) {
          __builtin_trap();
        }
        break;
      default:
        if (!r.has_value() && r.error().value != value) {
          // Reading the error is legitimate here; the comparison is incidental.
          // What matters is that reading it at all does not disturb the state.
        }
        break;
    }

    // Exactly one of the two alternatives is alive. A reinit that destroyed
    // neither, or both, lands here.
    if (r.has_value()) {
      if (g_live != 1 || g_live_other != 0) {
        __builtin_trap();
      }
    } else if (g_live != 0 || g_live_other != 1) {
      __builtin_trap();
    }
  }
}

void drive_variant(metl_fuzz::byte_reader& in) {
  using var = metl::variant<payload, other_payload>;
  var v{payload{0}};

  while (!in.empty()) {
    const std::uint32_t value = in.integer<std::uint32_t>();
    switch (in.byte() % 6u) {
      case 0:
        v = var{payload{value}};
        break;
      case 1:
        v = var{other_payload{value}};
        break;
      case 2:
        v.template emplace<payload>(value);
        break;
      case 3:
        v.template emplace<other_payload>(value);
        break;
      case 4: {
        // get_if is the total accessor; get<> asserts on the wrong alternative,
        // so it is only reached once get_if has proven which one is active.
        if (const payload* p = metl::get_if<payload>(&v)) {
          if (p->value != metl::get<payload>(v).value) {
            __builtin_trap();
          }
          if (v.index() != 0) {
            __builtin_trap();
          }
        } else if (const other_payload* q = metl::get_if<other_payload>(&v)) {
          if (q->value != metl::get<other_payload>(v).value || v.index() != 1) {
            __builtin_trap();
          }
        }
        break;
      }
      default: {
        // Self-assignment: the case where "destroy the old, construct the new"
        // destroys the thing it is about to read. Writing it as `v = v` is the
        // point of the test, so the diagnostic that objects to it is suppressed
        // HERE and nowhere wider -- a file-level or build-level suppression
        // would also hide a real accidental self-assignment.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif
        v = v;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        break;
      }
    }

    if (v.valueless_by_exception()) {
      __builtin_trap();  // unreachable without exceptions; if it happens, say so
    }
    // Exactly one alternative alive, and it is the one `index()` claims.
    const bool first = v.index() == 0;
    if (g_live != (first ? 1 : 0) || g_live_other != (first ? 0 : 1)) {
      __builtin_trap();
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  metl_fuzz::byte_reader in(data, size);

  switch (in.byte() % 3u) {
    case 0:
      drive_optional(in);
      break;
    case 1:
      drive_expected(in);
      break;
    default:
      drive_variant(in);
      break;
  }

  // Everything above went out of scope. Anything still counted is a payload the
  // destructor never reached.
  if (g_live != 0 || g_live_other != 0) {
    __builtin_trap();
  }
  return 0;
}
