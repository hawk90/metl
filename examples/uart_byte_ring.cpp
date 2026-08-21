// uart_byte_ring.cpp
//
// The shape a UART/DMA receive path actually has, with metl::spsc_byte_ring.
//
// A driver does not want to push bytes one at a time. It wants a REGION: an
// address and a length it can hand to the peripheral, and then a single "I moved
// n bytes" call. That is what `writable_span()` / `commit_write(n)` are, and it
// is the one thing `ring_buffer` cannot do -- its elements are deliberately not
// contiguous (see detail/ring_core.hpp), so it has no pointer to give out.
//
// The interesting part is the SEAM. The span stops at the physical end of the
// buffer, so a transfer near the end gets a short region even when plenty of
// space is free. That is not a bug to work around, it is the ring; the loop
// below simply asks again. This example is sized so the seam is crossed many
// times, because code that only ever runs on an empty ring never meets it.
//
// Modelled with a fake peripheral so the whole thing is deterministic and
// self-checking: it asserts the exact bytes and line boundaries, and returns
// non-zero if anything is off.
//
// NOT MODELLED: cache maintenance. On a part where DMA is not coherent with the
// data cache you must invalidate the region before reading what DMA deposited.
// metl cannot do that portably and does not pretend to -- see the header.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <metl/fixed_string.hpp>
#include <metl/spsc_byte_ring.hpp>

namespace {

// The traffic the "peripheral" will deliver: three newline-terminated lines.
const char* const kWire =
    "TEMP=21.5\n"
    "HUMIDITY=48\n"
    "STATUS=OK\n";

constexpr std::size_t kRingBytes = 16;  // deliberately smaller than the traffic

/// Stands in for a UART peripheral with a DMA engine: copies at most `span.size()`
/// bytes of the remaining wire data into the caller's region and reports how many
/// it moved. A real driver would program a transfer here and get this count from
/// the completion interrupt.
class fake_uart {
 public:
  std::size_t receive_into(metl::span<std::byte> destination) noexcept {
    const std::size_t remaining = length_of(kWire) - position_;
    if (remaining == 0 || destination.empty()) {
      return 0;
    }
    // Never fills the whole region: a real transfer usually ends on a timeout or
    // an idle line, so the ring must cope with partial commits.
    std::size_t count = remaining < destination.size() ? remaining : destination.size();
    if (count > 5) {
      count = 5;
    }
    for (std::size_t i = 0; i < count; ++i) {
      destination[i] = static_cast<std::byte>(kWire[position_ + i]);
    }
    position_ += count;
    return count;
  }

  bool done() const noexcept { return position_ == length_of(kWire); }

 private:
  static std::size_t length_of(const char* text) noexcept {
    std::size_t n = 0;
    while (text[n] != '\0') {
      ++n;
    }
    return n;
  }

  std::size_t position_ = 0;
};

/// Consumer side: pulls whole lines out of the ring without copying the bytes it
/// is only scanning past.
class line_assembler {
 public:
  /// @return How many complete lines were extracted this call.
  int drain(metl::spsc_byte_ring<kRingBytes>& ring) noexcept {
    int completed = 0;
    for (;;) {
      const metl::span<const std::byte> in = ring.readable_span();
      if (in.empty()) {
        return completed;
      }
      // Scan the contiguous run for a newline. Anything past the seam is simply
      // not in this span -- the next iteration gets it, because consuming moves
      // the read index off the end of the buffer and back to zero.
      std::size_t taken = 0;
      bool line_ended = false;
      for (std::size_t i = 0; i < in.size(); ++i) {
        taken = i + 1;
        if (static_cast<char>(in[i]) == '\n') {
          line_ended = true;
          break;
        }
        if (!current_.try_push_back(static_cast<char>(in[i]))) {
          overflowed_ = true;  // line longer than the buffer: recoverable, reported
        }
      }
      ring.consume(taken);
      if (line_ended) {
        if (line_count_ < kMaxLines) {
          lines_[line_count_] = current_;
          ++line_count_;
        }
        current_.clear();
        ++completed;
      }
    }
  }

  static constexpr std::size_t kMaxLines = 4;
  metl::fixed_string<32> lines_[kMaxLines];
  std::size_t line_count_ = 0;
  bool overflowed_ = false;

 private:
  metl::fixed_string<32> current_;
};

}  // namespace

int main() {
  metl::spsc_byte_ring<kRingBytes> ring;
  fake_uart uart;
  line_assembler assembler;

  std::size_t transfers = 0;
  std::size_t short_spans = 0;  // regions cut short by the seam

  // The super-loop: give the peripheral whatever contiguous room exists, then
  // drain what it delivered. Both sides make progress every pass.
  while (!uart.done() || ring.readable_size() != 0) {
    const metl::span<std::byte> out = ring.writable_span();
    if (!out.empty()) {
      if (out.size() < ring.writable_size()) {
        ++short_spans;  // free space wraps: this region stops at the buffer end
      }
      const std::size_t received = uart.receive_into(out);
      if (received > 0) {
        ring.commit_write(received);  // publish exactly what the transfer moved
        ++transfers;
      }
    }
    assembler.drain(ring);
  }

  // --- self-checks -------------------------------------------------------
  if (assembler.overflowed_) {
    return 1;  // no line here is longer than 32 characters
  }
  if (assembler.line_count_ != 3) {
    std::printf("expected 3 lines, got %zu\n", assembler.line_count_);
    return 2;
  }
  if (!(assembler.lines_[0] == metl::fixed_string<32>("TEMP=21.5"))) {
    return 3;
  }
  if (!(assembler.lines_[1] == metl::fixed_string<32>("HUMIDITY=48"))) {
    return 4;
  }
  if (!(assembler.lines_[2] == metl::fixed_string<32>("STATUS=OK"))) {
    return 5;
  }
  if (!ring.empty()) {
    return 6;
  }
  // The whole point of the example: the traffic is twice the ring, so the seam
  // must have been crossed. If this ever stops holding, the example has stopped
  // exercising the case it exists for.
  if (short_spans == 0) {
    std::printf("no wrapped region was ever handed out — the example is not testing the seam\n");
    return 7;
  }

  std::printf("uart_byte_ring: %zu transfers into a %zu-byte ring, %zu of them wrapped\n",
              transfers,
              kRingBytes,
              short_spans);
  for (std::size_t i = 0; i < assembler.line_count_; ++i) {
    std::printf("  line %zu: %s\n", i, assembler.lines_[i].c_str());
  }
  return 0;
}
