// libFuzzer harness for metl::parse.hpp.
//
// This is the header that most needs one. Every other target in fuzz/ is driven
// with an opcode stream the harness constructs; parse.hpp is the only part of
// METL whose *documented job* is to accept bytes somebody else chose. It landed
// without a harness, and the fuzzing coverage report is what said so.
//
// The fuzz input is copied into an exact-size heap buffer with NO NUL
// terminator, deliberately. The header's central claim is that it is bounded by
// the span and never scans for a terminator; against an ASan-poisoned exact
// allocation, a single byte of overread is a crash rather than a lucky read of
// whatever followed.
//
// `try_parse_*` are total -- any span is a valid argument -- so unlike the
// container harnesses there is no contract to keep on the way in. That frees the
// harness to check PROPERTIES instead, which is where the bug-finding power is:
//
//   1. The tail is a suffix. `tail.data() + tail.size()` must equal the end of
//      the input, and the tail must not be longer than the input. A parser that
//      miscounts consumed characters breaks this before it produces a wrong
//      number.
//   2. Widening agreement. If a narrow type parses successfully, the same text
//      parsed into a wider type must give the same value and the same tail.
//      This is the invariant that catches a bad overflow bound -- and the
//      overflow bound is computed per-width, from a `constexpr` limit that was
//      rewritten for code size, so it is exactly the arithmetic most likely to
//      be wrong on one width and right on another.
//   3. Overflow is never wrapped. If a narrow parse reports `out_of_range`, the
//      wide parse must either exceed the narrow type's range or overflow too.
//      A parser that silently truncated would show up here as a narrow failure
//      whose wide value fits.
//   4. Round trip against metl/format.hpp. Anything `format` writes must parse
//      back to the value it started from, with an empty tail. The two headers
//      are mirrors, so this is the strongest single property available.

#include "fuzz_helpers.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

#include <metl/format.hpp>
#include <metl/parse.hpp>
#include <metl/span.hpp>

namespace {

/// The tail must be a suffix of the input, always.
void check_tail(metl::span<const char> text, metl::span<const char> tail) {
  if (tail.size() > text.size()) {
    __builtin_trap();
  }
  if (tail.data() + tail.size() != text.data() + text.size()) {
    __builtin_trap();
  }
}

/// Properties 1-3 for one unsigned width against `std::uint64_t`.
template <typename Narrow>
void check_unsigned_width(metl::span<const char> text) {
  const auto narrow = metl::try_parse_uint<Narrow>(text);
  const auto wide = metl::try_parse_uint<std::uint64_t>(text);

  if (narrow) {
    check_tail(text, narrow->tail);
    // Widening agreement: a value a narrow type accepts must survive widening
    // unchanged, and must have consumed exactly the same characters.
    if (!wide) {
      __builtin_trap();
    }
    if (static_cast<std::uint64_t>(narrow->value) != wide->value) {
      __builtin_trap();
    }
    if (narrow->tail.size() != wide->tail.size()) {
      __builtin_trap();
    }
    return;
  }

  if (narrow.error() == metl::parse_error::out_of_range) {
    // Not wrapped: the value really did not fit. Either the wide parse also
    // overflowed, or it produced something the narrow type genuinely cannot
    // hold.
    if (wide) {
      if (wide->value <= static_cast<std::uint64_t>(std::numeric_limits<Narrow>::max())) {
        __builtin_trap();
      }
    } else if (wide.error() != metl::parse_error::out_of_range) {
      __builtin_trap();
    }
    return;
  }

  // `empty` and `not_a_number` are properties of the text alone, so every width
  // must agree on them.
  if (wide) {
    __builtin_trap();
  }
  if (wide.error() != narrow.error()) {
    __builtin_trap();
  }
}

template <typename Narrow>
void check_signed_width(metl::span<const char> text) {
  const auto narrow = metl::try_parse_int<Narrow>(text);
  const auto wide = metl::try_parse_int<std::int64_t>(text);

  if (narrow) {
    check_tail(text, narrow->tail);
    if (!wide) {
      __builtin_trap();
    }
    if (static_cast<std::int64_t>(narrow->value) != wide->value) {
      __builtin_trap();
    }
    if (narrow->tail.size() != wide->tail.size()) {
      __builtin_trap();
    }
    return;
  }

  if (narrow.error() == metl::parse_error::out_of_range) {
    if (wide) {
      const std::int64_t value = wide->value;
      const auto low = static_cast<std::int64_t>(std::numeric_limits<Narrow>::min());
      const auto high = static_cast<std::int64_t>(std::numeric_limits<Narrow>::max());
      if (value >= low && value <= high) {
        __builtin_trap();  // it fit after all -- the narrow bound is wrong
      }
    } else if (wide.error() != metl::parse_error::out_of_range) {
      __builtin_trap();
    }
    return;
  }

  if (wide) {
    __builtin_trap();
  }
  if (wide.error() != narrow.error()) {
    __builtin_trap();
  }
}

template <typename Narrow>
void check_hex_width(metl::span<const char> text) {
  const auto narrow = metl::try_parse_hex<Narrow>(text);
  const auto wide = metl::try_parse_hex<std::uint64_t>(text);

  if (narrow) {
    check_tail(text, narrow->tail);
    if (!wide || static_cast<std::uint64_t>(narrow->value) != wide->value ||
        narrow->tail.size() != wide->tail.size()) {
      __builtin_trap();
    }
    return;
  }
  if (narrow.error() == metl::parse_error::out_of_range) {
    if (wide) {
      if (wide->value <= static_cast<std::uint64_t>(std::numeric_limits<Narrow>::max())) {
        __builtin_trap();
      }
    } else if (wide.error() != metl::parse_error::out_of_range) {
      __builtin_trap();
    }
    return;
  }
  if (wide || wide.error() != narrow.error()) {
    __builtin_trap();
  }
}

/// Property 4: format writes it, parse reads it back, unchanged.
void check_round_trip(metl_fuzz::byte_reader& reader) {
  char scratch[32];
  const metl::span<char> out(scratch, sizeof scratch);

  {
    const auto value = reader.integer<std::uint64_t>();
    const metl::span<char> text = metl::format_uint(out, value);
    const auto read = metl::try_parse_uint<std::uint64_t>(metl::span<const char>(text.data(), text.size()));
    if (!read || read->value != value || !read->tail.empty()) {
      __builtin_trap();
    }
  }

  {
    const auto value = static_cast<std::int64_t>(reader.integer<std::uint64_t>());
    const metl::span<char> text = metl::format_int(out, value);
    const auto read = metl::try_parse_int<std::int64_t>(metl::span<const char>(text.data(), text.size()));
    if (!read || read->value != value || !read->tail.empty()) {
      __builtin_trap();
    }
  }

  {
    const auto value = reader.integer<std::uint64_t>();
    const auto letters = (reader.byte() & 1u) != 0u ? metl::hex_case::upper : metl::hex_case::lower;
    const metl::span<char> text = metl::format_hex(out, value, 0, letters);
    const auto read = metl::try_parse_hex<std::uint64_t>(metl::span<const char>(text.data(), text.size()));
    if (!read || read->value != value || !read->tail.empty()) {
      __builtin_trap();
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  // An exact-size copy with no terminator. Under ASan the byte one past the end
  // is poisoned, so a parser that scanned for a NUL would crash here instead of
  // reading whatever happened to follow the fuzz buffer.
  char* text_storage = size > 0 ? new char[size] : nullptr;
  for (std::size_t i = 0; i < size; ++i) {
    text_storage[i] = static_cast<char>(data[i]);
  }
  const metl::span<const char> text(text_storage, size);

  check_unsigned_width<std::uint8_t>(text);
  check_unsigned_width<std::uint16_t>(text);
  check_unsigned_width<std::uint32_t>(text);

  check_signed_width<std::int8_t>(text);
  check_signed_width<std::int16_t>(text);
  check_signed_width<std::int32_t>(text);

  check_hex_width<std::uint8_t>(text);
  check_hex_width<std::uint16_t>(text);
  check_hex_width<std::uint32_t>(text);

  // Scanning a stream: parse, then keep parsing from the tail. This is the
  // usage examples/wire_values.cpp is built on, and it is where a tail that is
  // not a strict suffix would loop forever or walk backwards.
  {
    metl::span<const char> rest = text;
    for (int step = 0; step < 8 && !rest.empty(); ++step) {
      const auto next = metl::try_parse_uint<std::uint32_t>(rest);
      if (!next) {
        // Not a number here; skip one character and try again, exactly as a
        // field scanner would.
        rest = rest.subspan(std::size_t{1});
        continue;
      }
      check_tail(rest, next->tail);
      if (next->tail.size() >= rest.size()) {
        __builtin_trap();  // consumed nothing while reporting success
      }
      rest = next->tail;
    }
  }

  delete[] text_storage;

  metl_fuzz::byte_reader reader(data, size);
  check_round_trip(reader);
  return 0;
}
