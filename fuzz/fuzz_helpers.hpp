// Shared fuzzing helpers for the metl libFuzzer harnesses.
//
// metl containers ASSERT (abort) on precondition violations (push past
// capacity, pop empty, out-of-range index). An abort inside a libFuzzer harness
// is reported as a crash, so a harness must ONLY perform contract-VALID
// operations: use the `try_*` members where they exist, check `size()`/
// `capacity()`/`empty()` before every asserting call, and bound every index by
// `% size()`. With the contract respected, any ASan/UBSan finding (heap/stack
// OOB, UB, use-after-poison, leak, uninitialized read) is a genuine defect.
//
// This header provides a tiny, dependency-free byte reader that turns the fuzz
// input into an OPCODE STREAM of valid operations plus bounded operands. It is
// deliberately independent of LLVM's <fuzzer/FuzzedDataProvider.h> so the
// harnesses stay portable and self-describing.

#ifndef METL_FUZZ_FUZZ_HELPERS_HPP
#define METL_FUZZ_FUZZ_HELPERS_HPP

#include <cstddef>
#include <cstdint>

namespace metl_fuzz {

/// Sequentially consumes the fuzz byte buffer as opcodes + operands.
///
/// All readers are total: once the buffer is exhausted they return 0 (or an
/// empty slice), so a harness loop terminates naturally on `remaining() == 0`
/// without ever reading out of bounds.
class byte_reader {
 public:
  byte_reader(const std::uint8_t* data, std::size_t size) noexcept : data_(data), size_(size), pos_(0) {}

  std::size_t remaining() const noexcept { return pos_ < size_ ? size_ - pos_ : 0; }
  bool empty() const noexcept { return remaining() == 0; }

  /// Next raw byte, or 0 when exhausted.
  std::uint8_t byte() noexcept { return pos_ < size_ ? data_[pos_++] : std::uint8_t{0}; }

  /// Little-endian unsigned integer of the requested width (missing bytes = 0).
  template <typename UInt>
  UInt integer() noexcept {
    UInt value = 0;
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
      value = static_cast<UInt>(value | (static_cast<UInt>(byte()) << (8u * i)));
    }
    return value;
  }

  /// Consumes a length-prefixed slice, copying up to `capacity - 1` bytes into
  /// `out` and NUL-terminating it. Returns the number of copied characters.
  /// The result is always a valid, NUL-terminated C string.
  std::size_t c_string(char* out, std::size_t capacity) noexcept {
    if (capacity == 0) {
      return 0;
    }
    std::size_t want = byte();
    const std::size_t room = capacity - 1;
    if (want > room) {
      want = room;
    }
    if (want > remaining()) {
      want = remaining();
    }
    std::size_t i = 0;
    for (; i < want; ++i) {
      out[i] = static_cast<char>(byte());
    }
    out[i] = '\0';
    return i;
  }

  /// Consumes a length-prefixed raw slice (may contain NUL). Returns a pointer
  /// into the underlying buffer and writes the length to `*len`.
  const std::uint8_t* slice(std::size_t* len) noexcept {
    std::size_t want = byte();
    if (want > remaining()) {
      want = remaining();
    }
    const std::uint8_t* begin = data_ + pos_;
    pos_ += want;
    *len = want;
    return begin;
  }

 private:
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_;
};

}  // namespace metl_fuzz

#endif  // METL_FUZZ_FUZZ_HELPERS_HPP
