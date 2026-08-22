// libFuzzer harness for the bounded sequence containers:
// fixed_deque, fixed_queue, fixed_stack, ring_buffer.
//
// One harness for four types because three of them are the same code. #14
// deduplicated ring_buffer and fixed_deque onto `detail::ring_core`, and
// fixed_queue is a thin wrapper over the same thing -- so an index-arithmetic
// bug in ring_core surfaces through whichever of them the input happens to
// select, and four near-identical harnesses would be four copies of this file
// for the same coverage. The first byte of the input picks the container; the
// rest is an opcode stream.
//
// WHAT THIS CATCHES THAT ASan CANNOT. These containers hold their elements in
// an INLINE array, not on the heap. A destructor that is skipped, or run twice,
// touches memory ASan considers perfectly valid -- it is a live member of a live
// object. So the element type here counts its own live instances, and every
// operation is checked against a model: the container's `size()` and the number
// of live payloads must agree, always. A pop that forgot to destroy, or a
// wrapping index that destroyed the wrong slot, shows up immediately.
//
// Contract-valid only. `front`/`back`/`top`/`pop` assert on an empty container,
// so each is guarded; the `try_` forms are used wherever capacity can refuse.

#include "fuzz_helpers.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/fixed_deque.hpp>
#include <metl/fixed_queue.hpp>
#include <metl/fixed_stack.hpp>
#include <metl/ring_buffer.hpp>

namespace {

constexpr std::size_t kCapacity = 8;

/// Live instances of `payload`, anywhere. The whole point of the harness: for
/// inline storage this is the only way a missed or doubled destructor is
/// visible.
int g_live = 0;

/// Deliberately non-trivial. A trivially-destructible element would let a
/// skipped destructor pass unnoticed, which is exactly the bug being hunted.
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

/// The container's own accounting must match reality, after every operation.
template <typename Container>
void check(const Container& container, std::size_t expected_size) {
  if (container.size() != expected_size) {
    __builtin_trap();
  }
  if (container.size() > kCapacity) {
    __builtin_trap();
  }
  if (container.empty() != (expected_size == 0)) {
    __builtin_trap();
  }
  if (container.full() != (expected_size == kCapacity)) {
    __builtin_trap();
  }
  // THE ONE THAT MATTERS. Elements live inline, so a leaked or double-run
  // destructor is invisible to ASan; only this counter sees it.
  if (g_live != static_cast<int>(expected_size)) {
    __builtin_trap();
  }
}

/// A FIFO model, used for the three ring-backed containers. Holds values only;
/// the live count is tracked separately by `payload` itself.
class fifo_model {
 public:
  void push_back(std::uint32_t v) {
    data_[(head_ + size_) % kCapacity] = v;
    ++size_;
  }
  void push_front(std::uint32_t v) {
    head_ = (head_ + kCapacity - 1) % kCapacity;
    data_[head_] = v;
    ++size_;
  }
  void pop_front() {
    head_ = (head_ + 1) % kCapacity;
    --size_;
  }
  void pop_back() { --size_; }
  std::uint32_t front() const { return data_[head_]; }
  std::uint32_t back() const { return data_[(head_ + size_ - 1) % kCapacity]; }
  std::uint32_t at(std::size_t i) const { return data_[(head_ + i) % kCapacity]; }
  void clear() { size_ = 0; }
  std::size_t size() const { return size_; }

 private:
  std::uint32_t data_[kCapacity] = {};
  std::size_t head_ = 0;
  std::size_t size_ = 0;
};

void drive_deque(metl_fuzz::byte_reader& in) {
  metl::fixed_deque<payload, kCapacity> deque;
  fifo_model model;

  while (!in.empty()) {
    const std::uint32_t value = in.integer<std::uint32_t>();
    switch (in.byte() % 7u) {
      case 0:
        if (deque.try_emplace_back(payload{value})) {
          model.push_back(value);
        }
        break;
      case 1:
        if (deque.try_emplace_front(payload{value})) {
          model.push_front(value);
        }
        break;
      case 2:
        if (!deque.empty()) {
          if (deque.front().value != model.front()) {
            __builtin_trap();
          }
          deque.pop_front();
          model.pop_front();
        }
        break;
      case 3:
        if (!deque.empty()) {
          if (deque.back().value != model.back()) {
            __builtin_trap();
          }
          deque.pop_back();
          model.pop_back();
        }
        break;
      case 4:  // every element, in order -- catches a wrapped index reading the
               // wrong slot without changing any size
        for (std::size_t i = 0; i < deque.size(); ++i) {
          if (deque[i].value != model.at(i)) {
            __builtin_trap();
          }
        }
        break;
      case 5:
        if (!deque.empty()) {
          if (deque.front().value != model.front() || deque.back().value != model.back()) {
            __builtin_trap();
          }
        }
        break;
      default:
        deque.clear();
        model.clear();
        break;
    }
    check(deque, model.size());
  }
}

void drive_ring(metl_fuzz::byte_reader& in) {
  metl::ring_buffer<payload, kCapacity> ring;
  fifo_model model;

  while (!in.empty()) {
    const std::uint32_t value = in.integer<std::uint32_t>();
    switch (in.byte() % 5u) {
      case 0:  // the operation only ring_buffer has: overwrite the oldest
        if (model.size() == kCapacity) {
          model.pop_front();
        }
        ring.push_overwrite(payload{value});
        model.push_back(value);
        break;
      case 1:
        if (ring.try_emplace_back(payload{value})) {
          model.push_back(value);
        }
        break;
      case 2:
        if (!ring.empty()) {
          if (ring.front().value != model.front()) {
            __builtin_trap();
          }
          ring.pop_front();
          model.pop_front();
        }
        break;
      case 3:
        for (std::size_t i = 0; i < ring.size(); ++i) {
          if (ring[i].value != model.at(i)) {
            __builtin_trap();
          }
        }
        break;
      default:
        ring.clear();
        model.clear();
        break;
    }
    check(ring, model.size());
  }
}

void drive_queue(metl_fuzz::byte_reader& in) {
  metl::fixed_queue<payload, kCapacity> queue;
  fifo_model model;

  while (!in.empty()) {
    const std::uint32_t value = in.integer<std::uint32_t>();
    switch (in.byte() % 4u) {
      case 0:
        if (queue.try_emplace(payload{value})) {
          model.push_back(value);
        }
        break;
      case 1:
        if (!queue.empty()) {
          if (queue.front().value != model.front() || queue.back().value != model.back()) {
            __builtin_trap();
          }
          queue.pop();
          model.pop_front();
        }
        break;
      case 2:
        if (!queue.empty() && queue.front().value != model.front()) {
          __builtin_trap();
        }
        break;
      default:
        queue.clear();
        model.clear();
        break;
    }
    check(queue, model.size());
  }
}

void drive_stack(metl_fuzz::byte_reader& in) {
  metl::fixed_stack<payload, kCapacity> stack;
  std::uint32_t model[kCapacity] = {};
  std::size_t depth = 0;

  while (!in.empty()) {
    const std::uint32_t value = in.integer<std::uint32_t>();
    switch (in.byte() % 4u) {
      case 0:
        if (stack.try_emplace(payload{value})) {
          model[depth++] = value;
        }
        break;
      case 1:
        if (!stack.empty()) {
          if (stack.top().value != model[depth - 1]) {
            __builtin_trap();
          }
          stack.pop();
          --depth;
        }
        break;
      case 2:
        if (!stack.empty() && stack.top().value != model[depth - 1]) {
          __builtin_trap();
        }
        break;
      default:
        stack.clear();
        depth = 0;
        break;
    }
    check(stack, depth);
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  metl_fuzz::byte_reader in(data, size);

  switch (in.byte() % 4u) {
    case 0:
      drive_deque(in);
      break;
    case 1:
      drive_ring(in);
      break;
    case 2:
      drive_queue(in);
      break;
    default:
      drive_stack(in);
      break;
  }

  // Everything went out of scope above, so nothing may still be alive. A
  // destructor that missed elements shows up here even if every intermediate
  // size check happened to agree.
  if (g_live != 0) {
    __builtin_trap();
  }
  return 0;
}
