// libFuzzer harness for metl::object_pool and metl::handle_pool.
//
// These two exist to be compared, and docs/SCOPE.md §7 makes the comparison an
// argument rather than a preference: a pointer into a recycled slot is
// indistinguishable from a live one, while a handle carries a generation and so
// the pool can DETECT the staleness. `examples/handles_and_pools.cpp`
// demonstrates that on one hand-written sequence. This drives it on every
// sequence the fuzzer can find.
//
// The property, stated as sharply as it can be:
//
//   A handle that was destroyed must NEVER resolve again -- not after the slot
//   is reused, not after it is reused many times, not after the pool is
//   cleared. And a handle that was not destroyed must ALWAYS resolve, to its
//   own value and nobody else's.
//
// The harness keeps every handle it has ever been given, live or dead, and
// re-checks all of them after every operation. That is what makes it a test of
// the generation counter rather than of the happy path: a stale handle only
// becomes dangerous once its slot has been handed to somebody else, which is
// many operations after the erase that made it stale.
//
// As in fuzz_sequence, the payload counts its own live instances, because these
// pools hold elements in inline storage where a missed destructor is invisible
// to ASan.

#include "fuzz_helpers.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/handle_pool.hpp>
#include <metl/object_pool.hpp>

namespace {

constexpr std::size_t kCapacity = 8;

int g_live = 0;

struct payload {
  std::uint32_t value;

  explicit payload(std::uint32_t v) noexcept : value(v) { ++g_live; }
  payload(const payload& other) noexcept : value(other.value) { ++g_live; }
  payload(payload&& other) noexcept : value(other.value) { ++g_live; }
  payload& operator=(const payload&) noexcept = default;
  payload& operator=(payload&&) noexcept = default;
  ~payload() { --g_live; }
};

using pool_type = metl::handle_pool<payload, kCapacity>;
using handle_type = pool_type::handle_type;

/// Every handle the pool has ever issued, with what we believe about it.
struct record {
  handle_type handle{};
  std::uint32_t value = 0;
  bool live = false;
};

void drive_handle_pool(metl_fuzz::byte_reader& in) {
  pool_type pool;

  // More slots than the pool has, so slots are reused many times over and stale
  // handles pile up behind live ones.
  constexpr std::size_t kTracked = 64;
  record seen[kTracked];
  std::size_t count = 0;
  std::size_t live_count = 0;

  auto recheck_all = [&]() {
    for (std::size_t i = 0; i < count; ++i) {
      const record& r = seen[i];
      const payload* resolved = pool.get(r.handle);
      if (r.live) {
        // A live handle must resolve, and to ITS value -- not to whatever now
        // occupies the slot.
        if (resolved == nullptr || resolved->value != r.value) {
          __builtin_trap();
        }
        if (!pool.contains(r.handle)) {
          __builtin_trap();
        }
      } else {
        // THE POINT OF THE TYPE. A destroyed handle must never resolve again,
        // however many times its slot has been recycled since.
        if (resolved != nullptr || pool.contains(r.handle)) {
          __builtin_trap();
        }
      }
    }
    if (pool.size() != live_count) {
      __builtin_trap();
    }
    if (g_live != static_cast<int>(live_count)) {
      __builtin_trap();
    }
    if (pool.available() != kCapacity - live_count) {
      __builtin_trap();
    }
  };

  while (!in.empty()) {
    const std::uint32_t value = in.integer<std::uint32_t>();
    switch (in.byte() % 4u) {
      case 0: {  // acquire
        const handle_type handle = pool.try_emplace(payload{value});
        if (handle.valid()) {
          if (live_count >= kCapacity) {
            __builtin_trap();  // handed out a slot it did not have
          }
          ++live_count;
          if (count < kTracked) {
            seen[count++] = record{handle, value, true};
          }
        } else if (live_count != kCapacity) {
          __builtin_trap();  // refused while it had room
        }
        break;
      }
      case 1: {  // release one we believe is live
        if (count == 0) {
          break;
        }
        const std::size_t pick = in.byte() % count;
        record& r = seen[pick];
        const bool destroyed = pool.destroy(r.handle);
        if (destroyed != r.live) {
          __builtin_trap();  // destroy() must answer "was this a live slot"
        }
        if (destroyed) {
          r.live = false;
          --live_count;
        }
        break;
      }
      case 2: {  // release something we already released -- double free
        if (count == 0) {
          break;
        }
        const std::size_t pick = in.byte() % count;
        if (!seen[pick].live && pool.destroy(seen[pick].handle)) {
          __builtin_trap();  // a second destroy must be refused, not repeated
        }
        break;
      }
      default: {
        pool.clear();
        for (std::size_t i = 0; i < count; ++i) {
          seen[i].live = false;
        }
        live_count = 0;
        break;
      }
    }
    recheck_all();
  }
}

void drive_object_pool(metl_fuzz::byte_reader& in) {
  metl::object_pool<payload, kCapacity> pool;

  // Pointers we currently believe are live, and the value each should hold.
  payload* held[kCapacity] = {};
  std::uint32_t expected[kCapacity] = {};
  std::size_t count = 0;

  auto recheck_all = [&]() {
    for (std::size_t i = 0; i < count; ++i) {
      if (held[i] == nullptr || held[i]->value != expected[i]) {
        __builtin_trap();
      }
      if (!pool.contains(held[i])) {
        __builtin_trap();
      }
    }
    if (pool.size() != count || g_live != static_cast<int>(count)) {
      __builtin_trap();
    }
    if (pool.available() != kCapacity - count) {
      __builtin_trap();
    }
  };

  while (!in.empty()) {
    const std::uint32_t value = in.integer<std::uint32_t>();
    switch (in.byte() % 3u) {
      case 0: {
        payload* p = pool.try_emplace(payload{value});
        if (p != nullptr) {
          if (count >= kCapacity) {
            __builtin_trap();
          }
          held[count] = p;
          expected[count] = value;
          ++count;
        } else if (count != kCapacity) {
          __builtin_trap();
        }
        break;
      }
      case 1: {
        if (count == 0) {
          break;
        }
        const std::size_t pick = in.byte() % count;
        if (!pool.destroy(held[pick])) {
          __builtin_trap();  // we believed it was live
        }
        // No stale-pointer check here, deliberately: object_pool CANNOT detect
        // one, which is the whole reason handle_pool exists. Checking for it
        // would be asserting a guarantee the type does not make.
        held[pick] = held[count - 1];
        expected[pick] = expected[count - 1];
        --count;
        break;
      }
      default:
        pool.clear();
        count = 0;
        break;
    }
    recheck_all();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  metl_fuzz::byte_reader in(data, size);

  if ((in.byte() & 1u) != 0u) {
    drive_handle_pool(in);
  } else {
    drive_object_pool(in);
  }

  if (g_live != 0) {
    __builtin_trap();
  }
  return 0;
}
