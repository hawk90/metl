// libFuzzer harness for metl::optional and metl::expected.
//
// These two share one hazard and it is the reason they are fuzzed together:
// each holds its payload in INLINE aligned storage and changes which object
// lives there by destroying one and constructing another. Every assignment,
// every `emplace`, every `reset` is a hand-written destroy/construct pair, and
// `expected` has to do it between DIFFERENT types.
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
//   none; an expected holds exactly one of value or error -- after EVERY
//   operation, including the ones that switch between them.
//
// Counting is not enough on its own. A move assignment that silently does
// nothing keeps exactly one live member -- the OLD one -- and the counts
// balance. So the expected driver also keeps a model of what each object
// should hold, and compares value and state after every operation.
//
// Only contract-valid operations. `value()` and `error()` assert when the wrong
// state is active, so every unchecked accessor is guarded by the corresponding
// query first -- which is what the library tells callers to do.

#include "fuzz_helpers.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/expected.hpp>
#include <metl/optional.hpp>

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

/// A second, distinct counted type, so `expected` switching between value and
/// error is visible as two separate counts rather than one that happens to
/// balance.
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
      METL_FUZZ_TRAP();
    }
    if (opt.has_value() && opt->value != opt.value().value) {
      METL_FUZZ_TRAP();
    }
  }
}

/// What a `metl::expected<payload, other_payload>` should hold, kept alongside
/// it. The counts above prove exactly one member is alive; this proves it is
/// the RIGHT member with the RIGHT value. A move assignment that silently did
/// nothing passed the counts -- the old member stays alive, one of each, the
/// books balance -- and only a value comparison notices.
struct expected_model {
  bool has_value;
  std::uint32_t value;
  std::uint64_t error;
};

using vocab_result = metl::expected<payload, other_payload>;

bool matches(const vocab_result& actual, const expected_model& model) {
  if (actual.has_value() != model.has_value) {
    return false;
  }
  return model.has_value ? actual->value == model.value && actual.value().value == model.value
                         : actual.error().value == model.error;
}

void drive_expected(metl_fuzz::byte_reader& in) {
  vocab_result r{payload{0}};
  vocab_result s{metl::unexpected<other_payload>(other_payload{0})};
  expected_model mr{true, 0, 0};
  expected_model ms{false, 0, 0};

  while (!in.empty()) {
    const std::uint32_t value = in.integer<std::uint32_t>();
    switch (in.byte() % 10u) {
      case 0:  // move-assign from a temporary
        r = vocab_result{payload{value}};
        mr = {true, value, 0};
        break;
      case 1:  // value -> error and back: reinitialisation between DIFFERENT
               // types, the path with the most to get wrong
        r = vocab_result{metl::unexpected<other_payload>(other_payload{value})};
        mr = {false, 0, value};
        break;
      case 2:
        r.emplace(value);
        mr = {true, value, 0};
        break;
      case 3:
        r.emplace_error(std::uint64_t{value});
        mr = {false, 0, value};
        break;
      case 4:
        s.emplace(value);
        ms = {true, value, 0};
        break;
      case 5:
        s = metl::unexpected<other_payload>(other_payload{value});
        ms = {false, 0, value};
        break;
      case 6:  // copy-assign between two named objects, every state pairing
        r = s;
        mr = ms;
        break;
      case 7:  // move-assign from a named object. payload's move copies, so the
               // source keeps its value and the model of it does not change.
        r = static_cast<vocab_result&&>(s);
        mr = ms;
        break;
      case 8: {
        r.swap(s);
        const expected_model held = mr;
        mr = ms;
        ms = held;
        break;
      }
      default:
        // Reading the error is legitimate here; the comparison is incidental.
        // What matters is that reading it at all does not disturb the state.
        if (!r.has_value()) {
          static_cast<void>(r.error().value != value);
        }
        break;
    }

    if (!matches(r, mr) || !matches(s, ms)) {
      METL_FUZZ_TRAP();
    }

    // Exactly one of the two alternatives is alive in each. A reinit that
    // destroyed neither, or both, lands here.
    const int values = (r.has_value() ? 1 : 0) + (s.has_value() ? 1 : 0);
    if (g_live != values || g_live_other != 2 - values) {
      METL_FUZZ_TRAP();
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  metl_fuzz::byte_reader in(data, size);

  if (in.byte() % 2u == 0) {
    drive_optional(in);
  } else {
    drive_expected(in);
  }

  // Everything above went out of scope. Anything still counted is a payload the
  // destructor never reached.
  if (g_live != 0 || g_live_other != 0) {
    METL_FUZZ_TRAP();
  }
  return 0;
}
