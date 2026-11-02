// object_pool vs handle_pool. Both now allocate in O(1) from an intrusive free
// list; object_pool once scanned linearly for the first inactive slot, which is
// what this benchmark was written to show (#20). Capacity stays the axis, so a
// return of the scan would show as a gap that opens with capacity.

#include "metl_bench.hpp"

#include <cstdint>

#include <metl/handle_pool.hpp>
#include <metl/object_pool.hpp>

namespace {

// Churn: fill the pool, then repeatedly free and re-allocate the LAST slot. That
// was object_pool's worst case while it scanned: every try_emplace rescanned the
// whole occupied prefix before reaching the hole.
template <std::size_t Capacity>
void object_pool_tail_churn(std::uint64_t iterations) {
  metl::object_pool<std::uint32_t, Capacity> pool;
  std::uint32_t* slots[Capacity] = {};
  for (std::size_t i = 0; i < Capacity; ++i) {
    slots[i] = pool.try_emplace(static_cast<std::uint32_t>(i));
  }

  for (std::uint64_t n = 0; n < iterations; ++n) {
    pool.destroy(slots[Capacity - 1]);
    slots[Capacity - 1] = pool.try_emplace(1u);
    metl_bench::do_not_optimize(slots[Capacity - 1]);
  }
}

template <std::size_t Capacity>
void handle_pool_tail_churn(std::uint64_t iterations) {
  metl::handle_pool<std::uint32_t, Capacity> pool;
  typename metl::handle_pool<std::uint32_t, Capacity>::handle_type handles[Capacity] = {};
  for (std::size_t i = 0; i < Capacity; ++i) {
    handles[i] = pool.try_emplace(static_cast<std::uint32_t>(i));
  }

  for (std::uint64_t n = 0; n < iterations; ++n) {
    pool.destroy(handles[Capacity - 1]);
    handles[Capacity - 1] = pool.try_emplace(1u);
    metl_bench::do_not_optimize(handles[Capacity - 1]);
  }
}

// Resolve a handle to a pointer. object_pool has no equivalent — it hands out
// raw pointers — so this measures what the safety costs: a bounds check, an
// active check and a generation compare.
template <std::size_t Capacity>
void handle_pool_resolve(std::uint64_t iterations) {
  metl::handle_pool<std::uint32_t, Capacity> pool;
  const auto handle = pool.try_emplace(7u);

  for (std::uint64_t n = 0; n < iterations; ++n) {
    metl_bench::do_not_optimize(pool.get(handle));
  }
}

}  // namespace

int main(int argc, char** argv) {
  const auto cfg = metl_bench::config::from_args(argc, argv);
  metl_bench::header(cfg, "pools — alloc/free churn at the tail (object_pool's worst case)");

  metl_bench::run("object_pool<4>  alloc+free", cfg, object_pool_tail_churn<4>);
  metl_bench::run("handle_pool<4>  alloc+free", cfg, handle_pool_tail_churn<4>);

  metl_bench::run("object_pool<64> alloc+free", cfg, object_pool_tail_churn<64>);
  metl_bench::run("handle_pool<64> alloc+free", cfg, handle_pool_tail_churn<64>);

  metl_bench::run("object_pool<1024> alloc+free", cfg, object_pool_tail_churn<1024>);
  metl_bench::run("handle_pool<1024> alloc+free", cfg, handle_pool_tail_churn<1024>);

  metl_bench::header(cfg, "pools — handle resolution (the cost of use-after-free detection)");
  metl_bench::run("handle_pool<64> get()", cfg, handle_pool_resolve<64>);

  return metl_bench::require_selection(cfg);
}
