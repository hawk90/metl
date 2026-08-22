// libFuzzer harness for metl::format.hpp.
//
// `fuzz_parse` already round-trips format's output, but only through buffers big
// enough to hold it. That misses the half of this header that is actually
// delicate: deciding whether the result FITS, and refusing when it does not.
// Every function here computes a digit count and compares it against a
// caller-supplied span before writing a single byte, and getting that comparison
// wrong by one is an out-of-bounds write into somebody's stack buffer.
//
// So this harness fuzzes the BUFFER as hard as the value. Both come from the
// fuzz input, the buffer is an exact-size heap allocation (so ASan poisons the
// byte after it), and it is deliberately biased small -- a 1- or 2-character
// buffer is where the boundary lives, and a uniformly random size would almost
// never produce one.
//
// The properties:
//
//   1. Nothing is written outside the span. Enforced by ASan against the exact
//      allocation, plus a canary byte pattern checked after every call.
//   2. Refusal is total. When the result does not fit, the returned span is
//      empty AND the buffer is unmodified -- the header promises "not modified
//      at all", which is stronger than "no overflow".
//   3. Success is exact. A non-empty result must be exactly as long as the
//      digits require, and must round-trip back through metl::parse to the value
//      it started from.
//   4. try_ and asserting forms agree. Where try_ succeeds, the asserting form
//      must produce identical bytes. (The asserting form is only called when
//      try_ has already proven it fits -- calling it otherwise is a contract
//      violation, not a bug to find.)
//   5. Fixed-width hex refuses rather than truncates. `format_hex(out, v, n)`
//      with n too small for v must return empty: a register dump missing its
//      high nibbles is worse than no dump, and the header says so.

#include "fuzz_helpers.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <metl/format.hpp>
#include <metl/parse.hpp>
#include <metl/span.hpp>

namespace {

constexpr std::uint8_t kCanary = 0xA5u;

/// An exact-size heap buffer prefilled with a canary, so "did not modify the
/// buffer" is checkable and an overrun is an ASan report rather than luck.
class scratch {
 public:
  explicit scratch(std::size_t size) : size_(size), data_(size > 0 ? new char[size] : nullptr) { reset(); }
  ~scratch() { delete[] data_; }

  scratch(const scratch&) = delete;
  scratch& operator=(const scratch&) = delete;

  void reset() {
    for (std::size_t i = 0; i < size_; ++i) {
      data_[i] = static_cast<char>(kCanary);
    }
  }

  metl::span<char> span() { return metl::span<char>(data_, size_); }

  /// True when every byte still holds the canary.
  bool untouched() const {
    for (std::size_t i = 0; i < size_; ++i) {
      if (static_cast<std::uint8_t>(data_[i]) != kCanary) {
        return false;
      }
    }
    return true;
  }

  /// True when nothing past `written` was disturbed.
  bool clean_after(std::size_t written) const {
    for (std::size_t i = written; i < size_; ++i) {
      if (static_cast<std::uint8_t>(data_[i]) != kCanary) {
        return false;
      }
    }
    return true;
  }

  std::size_t size() const { return size_; }

 private:
  std::size_t size_;
  char* data_;
};

void check_unsigned(scratch& buffer, std::uint64_t value) {
  buffer.reset();
  const metl::span<char> text = metl::try_format_uint(buffer.span(), value);

  if (text.empty()) {
    // Property 2: refusal leaves the buffer exactly as it was.
    if (!buffer.untouched()) {
      __builtin_trap();
    }
    return;
  }

  // Property 1/3: wrote inside the span, and only as far as it claimed.
  if (text.size() > buffer.size() || text.data() != buffer.span().data()) {
    __builtin_trap();
  }
  if (!buffer.clean_after(text.size())) {
    __builtin_trap();
  }

  // Property 3: round trips.
  const auto read = metl::try_parse_uint<std::uint64_t>(metl::span<const char>(text.data(), text.size()));
  if (!read || read->value != value || !read->tail.empty()) {
    __builtin_trap();
  }

  // Property 4: the asserting form agrees, now that fitting is proven.
  char mirror[24];
  const metl::span<char> again = metl::format_uint(metl::span<char>(mirror, sizeof mirror), value);
  if (again.size() != text.size() || std::memcmp(again.data(), text.data(), text.size()) != 0) {
    __builtin_trap();
  }
}

void check_signed(scratch& buffer, std::int64_t value) {
  buffer.reset();
  const metl::span<char> text = metl::try_format_int(buffer.span(), value);

  if (text.empty()) {
    if (!buffer.untouched()) {
      __builtin_trap();
    }
    return;
  }
  if (text.size() > buffer.size() || text.data() != buffer.span().data()) {
    __builtin_trap();
  }
  if (!buffer.clean_after(text.size())) {
    __builtin_trap();
  }

  const auto read = metl::try_parse_int<std::int64_t>(metl::span<const char>(text.data(), text.size()));
  if (!read || read->value != value || !read->tail.empty()) {
    __builtin_trap();
  }

  char mirror[24];
  const metl::span<char> again = metl::format_int(metl::span<char>(mirror, sizeof mirror), value);
  if (again.size() != text.size() || std::memcmp(again.data(), text.data(), text.size()) != 0) {
    __builtin_trap();
  }
}

void check_hex(scratch& buffer, std::uint64_t value, std::size_t digits, metl::hex_case letters) {
  buffer.reset();
  const metl::span<char> text = metl::try_format_hex(buffer.span(), value, digits, letters);

  if (text.empty()) {
    if (!buffer.untouched()) {
      __builtin_trap();
    }
    return;
  }
  if (text.size() > buffer.size() || text.data() != buffer.span().data()) {
    __builtin_trap();
  }
  if (!buffer.clean_after(text.size())) {
    __builtin_trap();
  }

  // Property 5: a fixed width is honoured exactly, or refused. Never truncated.
  if (digits != 0 && text.size() != digits) {
    __builtin_trap();
  }

  const auto read = metl::try_parse_hex<std::uint64_t>(metl::span<const char>(text.data(), text.size()));
  if (!read || read->value != value || !read->tail.empty()) {
    __builtin_trap();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  metl_fuzz::byte_reader in(data, size);

  while (in.remaining() >= 10) {
    // Biased SMALL on purpose: 0..23 covers every boundary a 64-bit value can
    // hit, and the interesting cases are all at the bottom of that range. A
    // uniform size over a wide range would spend the whole budget on buffers
    // that trivially fit.
    const std::size_t width = in.byte() % 24u;
    scratch buffer(width);

    const auto value = in.integer<std::uint64_t>();
    switch (in.byte() % 3u) {
      case 0:
        check_unsigned(buffer, value);
        break;
      case 1:
        check_signed(buffer, static_cast<std::int64_t>(value));
        break;
      default: {
        // `digits` bounded to something a 64-bit value could plausibly want,
        // including 0 ("as many as it needs") and widths too small to hold it.
        const std::size_t digits = in.byte() % 20u;
        const auto letters = (in.byte() & 1u) != 0u ? metl::hex_case::upper : metl::hex_case::lower;
        check_hex(buffer, value, digits, letters);
        break;
      }
    }
  }

  return 0;
}
