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
// Counting is not enough on its own. A move assignment that silently does
// nothing keeps exactly one live member -- the OLD one -- and the counts
// balance. So the expected and variant drivers also keep a model of what each
// object should hold, and compare value and state after every operation.
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

/// What a `metl::variant<payload, other_payload>` should hold.
struct variant_model {
  std::size_t index;
  std::uint64_t value;
};

using vocab_variant = metl::variant<payload, other_payload>;

/// Reads the active alternative through visit, so the oracle also covers
/// dispatch on every value category, not only `get_if`.
struct read_value {
  std::uint64_t operator()(const payload& p) const noexcept { return p.value; }
  std::uint64_t operator()(const other_payload& p) const noexcept { return p.value; }
};

bool matches(const vocab_variant& actual, const variant_model& model) {
  if (actual.valueless_by_exception() || actual.index() != model.index) {
    return false;
  }
  if (const payload* p = metl::get_if<payload>(&actual)) {
    if (model.index != 0 || p->value != model.value || metl::get<payload>(actual).value != model.value) {
      return false;
    }
  } else if (const other_payload* q = metl::get_if<other_payload>(&actual)) {
    if (model.index != 1 || q->value != model.value ||
        metl::get<other_payload>(actual).value != model.value) {
      return false;
    }
  } else {
    return false;
  }
  return metl::visit(read_value{}, actual) == model.value;
}

void drive_variant(metl_fuzz::byte_reader& in) {
  vocab_variant v{payload{0}};
  vocab_variant w{other_payload{0}};
  variant_model mv{0, 0};
  variant_model mw{1, 0};

  while (!in.empty()) {
    const std::uint32_t value = in.integer<std::uint32_t>();
    switch (in.byte() % 10u) {
      case 0:
        v = vocab_variant{payload{value}};
        mv = {0, value};
        break;
      case 1:
        v = vocab_variant{other_payload{value}};
        mv = {1, value};
        break;
      case 2:
        v.template emplace<payload>(value);
        mv = {0, value};
        break;
      case 3:
        v.template emplace<other_payload>(value);
        mv = {1, value};
        break;
      case 4:
        w.template emplace<payload>(value);
        mw = {0, value};
        break;
      case 5:
        w = other_payload{value};
        mw = {1, value};
        break;
      case 6:  // copy-assign between named objects, same and different index
        v = w;
        mv = mw;
        break;
      case 7:  // move-assign; payload's move copies, so w keeps its value
        v = static_cast<vocab_variant&&>(w);
        mv = mw;
        break;
      case 8:  // rvalue visit of a copy: reaches the rvalue overload
        if (metl::visit(read_value{}, vocab_variant{v}) != mv.value) {
          METL_FUZZ_TRAP();
        }
        break;
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

    // valueless_by_exception() is unreachable without a throwing alternative;
    // matches() rejects it, so if it happens it is reported.
    if (!matches(v, mv) || !matches(w, mw)) {
      METL_FUZZ_TRAP();
    }
    // Exactly one alternative alive in each, and it is the one `index()` claims.
    const int firsts = (v.index() == 0 ? 1 : 0) + (w.index() == 0 ? 1 : 0);
    if (g_live != firsts || g_live_other != 2 - firsts) {
      METL_FUZZ_TRAP();
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
    METL_FUZZ_TRAP();
  }
  return 0;
}
