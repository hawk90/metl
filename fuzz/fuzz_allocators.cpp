// libFuzzer harness for metl::arena_allocator / metl::static_allocator.
//
// Drives both bump/LIFO allocators with an opcode stream of CONTRACT-VALID
// operations only: try_allocate / try_new / allocate-via-try / mark / rewind /
// reset. Every raw allocation is fully written (memset over exactly the
// requested bytes) so ASan flags any pointer the allocator hands back that lies
// outside its backing storage, and UBSan flags the size/alignment arithmetic.
//
// The allocators are heap-allocated so ASan redzones bracket the whole object:
// a bounds-math bug that returns a pointer past the inline buffer becomes a
// heap-buffer-overflow. rewind is only ever called with LIFO marks (each <=
// the current offset), which is exactly the documented precondition.

#include "fuzz_helpers.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <metl/arena_allocator.hpp>
#include <metl/static_allocator.hpp>

namespace {

constexpr std::size_t kArenaBytes = 256;
constexpr std::size_t kStaticBytes = 256;

// Alignment operand: a power of two in [1, alignof(max_align_t)].
std::size_t bounded_alignment(std::uint8_t raw) {
  std::size_t align = std::size_t{1} << (raw % 5u);  // 1,2,4,8,16
  if (align > alignof(std::max_align_t)) {
    align = alignof(std::max_align_t);
  }
  return align;
}

void drive_arena(metl_fuzz::byte_reader& in) {
  auto arena = std::make_unique<metl::arena_allocator<kArenaBytes>>();
  std::vector<metl::arena_allocator<kArenaBytes>::mark_type> marks;

  while (!in.empty()) {
    switch (in.byte() % 6u) {
      case 0: {  // raw allocate + full write
        const std::size_t bytes = in.byte() % (kArenaBytes / 2 + 1);
        const std::size_t align = bounded_alignment(in.byte());
        const std::size_t before = arena->used();
        void* p = arena->allocate(bytes, align);
        if (p != nullptr) {
          std::memset(p, 0xA5, bytes);
          if (arena->used() <= before || arena->used() > arena->capacity()) {
            __builtin_trap();
          }
        }
        break;
      }
      case 1: {  // typed emplace (destructor tracked for rewind)
        std::uint64_t* p = arena->try_emplace<std::uint64_t>(in.integer<std::uint64_t>());
        if (p != nullptr) {
          volatile std::uint64_t v = *p;
          (void)v;
        }
        break;
      }
      case 2: {  // capture a savepoint
        marks.push_back(arena->mark());
        break;
      }
      case 3: {  // rewind to the most recent still-valid (LIFO) savepoint
        if (!marks.empty()) {
          const auto target = marks.back();
          marks.pop_back();
          arena->rewind(target);  // LIFO: target.offset <= used()
        }
        break;
      }
      case 4: {  // reset everything
        arena->reset();
        marks.clear();
        if (!arena->empty()) {
          __builtin_trap();
        }
        break;
      }
      default: {
        if (arena->used() > arena->capacity()) {
          __builtin_trap();
        }
        break;
      }
    }
  }
}

void drive_static(metl_fuzz::byte_reader& in) {
  auto alloc = std::make_unique<metl::static_allocator<std::uint32_t, kStaticBytes>>();

  while (!in.empty()) {
    switch (in.byte() % 4u) {
      case 0: {  // try_allocate(count) + full write over count elements
        const std::size_t count = in.byte() % 16u;
        std::uint32_t* p = alloc->try_allocate(count);
        if (p != nullptr) {
          for (std::size_t i = 0; i < count; ++i) {
            p[i] = static_cast<std::uint32_t>(i);
          }
        }
        break;
      }
      case 1: {  // try_new — construct one element
        std::uint32_t* p = alloc->try_new(in.integer<std::uint32_t>());
        if (p != nullptr) {
          volatile std::uint32_t v = *p;
          (void)v;
        }
        break;
      }
      case 2: {  // reset
        alloc->reset();
        break;
      }
      default: {
        if (alloc->used_bytes() > alloc->capacity_bytes()) {
          __builtin_trap();
        }
        break;
      }
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  // Split the input: the first byte selects which allocator to exercise so both
  // get real coverage over a run.
  metl_fuzz::byte_reader in(data, size);
  if ((in.byte() & 1u) == 0u) {
    drive_arena(in);
  } else {
    drive_static(in);
  }
  return 0;
}
