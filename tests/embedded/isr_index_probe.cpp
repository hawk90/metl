// The ISR-facing index operations, built for a core, so CI can check that they
// call no atomic library function (#314).
//
// ARMv6-M has no exclusive loads or stores, and Clang lowers every atomic there
// to __atomic_load_4 / __atomic_store_4 calls, even a relaxed word load. What
// those resolve to depends on the runtime, and compiler-rt's version takes a
// lock that an interrupt can deadlock against. The CI job builds this file and
// fails if the object references any __atomic_ symbol.

#include <cstddef>

#include <metl/spsc_byte_ring.hpp>
#include <metl/spsc_queue.hpp>

namespace {
metl::spsc_queue<int, 16> g_queue;
metl::spsc_byte_ring<64> g_ring;
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
