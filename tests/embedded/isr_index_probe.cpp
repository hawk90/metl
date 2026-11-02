// The ISR-facing index operations, and atomic_ref's load and store, built for a
// core, so CI can check that they call no atomic library function (#314, #585).
// docs/CHOOSING.md offers atomic_ref's load and store for a flag an ISR shares.
//
// ARMv6-M has no exclusive loads or stores, and Clang lowers every atomic there
// to __atomic_load_4 / __atomic_store_4 calls, even a relaxed word load. What
// those resolve to depends on the runtime, and compiler-rt's version takes a
// lock that an interrupt can deadlock against. The CI job builds this file and
// fails if the object references any __atomic_ symbol.

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <metl/atomic_ref.hpp>
#include <metl/spsc_byte_ring.hpp>
#include <metl/spsc_queue.hpp>

namespace {
metl::spsc_queue<int, 16> g_queue;
metl::spsc_byte_ring<64> g_ring;

// One of each size atomic_ref takes as a plain access, and one with padding,
// which goes through the word path elsewhere.
std::uint8_t g_byte;
alignas(2) std::uint16_t g_half;
alignas(4) std::uint32_t g_word;
struct padded {
  std::uint8_t tag;
  std::uint16_t value;
};
alignas(4) padded g_padded;
}  // namespace

bool probe_push(int value) {
  return g_queue.try_push(value);
}
bool probe_pop(int& out) {
  return g_queue.try_pop(out);
}
bool probe_queue_empty() {
  return g_queue.empty();
}
bool probe_write(const std::byte* data, std::size_t size) {
  return g_ring.try_write(metl::span<const std::byte>(data, size));
}
std::size_t probe_read(std::byte* data, std::size_t size) {
  return g_ring.read(metl::span<std::byte>(data, size));
}
std::size_t probe_ring_readable() {
  return g_ring.readable_size();
}
std::size_t probe_ring_writable() {
  return g_ring.writable_size();
}
std::uint32_t probe_flag_load() {
  return metl::atomic_ref<std::uint32_t>(g_word).load(std::memory_order_acquire);
}
void probe_flag_store(std::uint32_t value) {
  metl::atomic_ref<std::uint32_t>(g_word).store(value, std::memory_order_release);
}
std::uint32_t probe_flag_seq_cst(std::uint32_t value) {
  metl::atomic_ref<std::uint32_t>(g_word).store(value);
  return metl::atomic_ref<std::uint32_t>(g_word).load();
}
std::uint16_t probe_half(std::uint16_t value) {
  metl::atomic_ref<std::uint16_t>(g_half).store(value, std::memory_order_relaxed);
  return metl::atomic_ref<std::uint16_t>(g_half).load(std::memory_order_relaxed);
}
std::uint8_t probe_byte(std::uint8_t value) {
  metl::atomic_ref<std::uint8_t>(g_byte).store(value);
  return metl::atomic_ref<std::uint8_t>(g_byte).load();
}
std::uint16_t probe_padded(std::uint16_t value) {
  metl::atomic_ref<padded>(g_padded).store(padded{1, value});
  return metl::atomic_ref<padded>(g_padded).load().value;
}
