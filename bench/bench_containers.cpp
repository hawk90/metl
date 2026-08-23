// Container hot paths. Nothing here is compared against the standard library —
// the point is a baseline that makes a future regression visible, not a claim
// that METL is faster than something else.

#include "metl_bench.hpp"

#include <cstdint>

#include <metl/crc32.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/span.hpp>
#include <metl/static_unordered_map.hpp>

namespace {

void fixed_vector_fill_clear(std::uint64_t iterations) {
  metl::fixed_vector<std::uint32_t, 64> vec;
  for (std::uint64_t n = 0; n < iterations; ++n) {
    vec.clear();
    for (std::uint32_t i = 0; i < 64; ++i) {
      (void)vec.try_push_back(i);
    }
    metl_bench::do_not_optimize(vec.size());
  }
}

// push_overwrite is the steady-state path for a telemetry-style ring: once full,
// every push also evicts, so this measures both halves.
void ring_buffer_push_overwrite(std::uint64_t iterations) {
  metl::ring_buffer<std::uint32_t, 64> ring;
  for (std::uint64_t n = 0; n < iterations; ++n) {
    metl_bench::do_not_optimize(ring.push_overwrite(static_cast<std::uint32_t>(n)));
  }
}

// The `do_not_optimize` calls here are load-bearing, and the first version of
// this benchmark proved it: it discarded try_push_back's result and anchored on
// `ring.size()`, which is invariantly 0 after a matched push and pop. The
// compiler proved the ring stays empty and deleted both operations, leaving a
// counting loop. Nothing noticed for as long as it had been there -- the
// wall-clock number was simply very good. tools/check_instructions.py measured
// 2.06 instructions per iteration and MIN_MEANINGFUL_COUNT now fails the build
// on a benchmark this thin, so the class of mistake cannot come back quietly.
void ring_buffer_push_pop(std::uint64_t iterations) {
  metl::ring_buffer<std::uint32_t, 64> ring;
  for (std::uint64_t n = 0; n < iterations; ++n) {
    metl_bench::do_not_optimize(ring.try_push_back(static_cast<std::uint32_t>(n)));
    metl_bench::do_not_optimize(ring.front());
    ring.pop_front();
    metl_bench::clobber_memory();
  }
}

// Lookup benchmarks are split hit/miss on purpose: a miss walks the whole probe
// sequence for the open-addressed map and the whole binary search for the
// sorted one, so averaging the two would hide which path regressed.
metl::flat_map<std::uint32_t, std::uint32_t, 256> make_flat_map() {
  metl::flat_map<std::uint32_t, std::uint32_t, 256> map;
  for (std::uint32_t i = 0; i < 256; ++i) {
    (void)map.try_emplace(i * 3u, i);
  }
  return map;
}

void flat_map_find_hit(std::uint64_t iterations) {
  auto map = make_flat_map();
  for (std::uint64_t n = 0; n < iterations; ++n) {
    metl_bench::do_not_optimize(map.find(static_cast<std::uint32_t>((n % 256) * 3u)));
  }
}

void flat_map_find_miss(std::uint64_t iterations) {
  auto map = make_flat_map();
  for (std::uint64_t n = 0; n < iterations; ++n) {
    metl_bench::do_not_optimize(map.find(static_cast<std::uint32_t>((n % 256) * 3u + 1u)));
  }
}

void unordered_map_find_hit(std::uint64_t iterations) {
  metl::static_unordered_map<std::uint32_t, std::uint32_t, 256> map;
  for (std::uint32_t i = 0; i < 200; ++i) {
    (void)map.try_emplace(i * 3u, i);
  }
  for (std::uint64_t n = 0; n < iterations; ++n) {
    metl_bench::do_not_optimize(map.find(static_cast<std::uint32_t>((n % 200) * 3u)));
  }
}

void unordered_map_find_miss(std::uint64_t iterations) {
  metl::static_unordered_map<std::uint32_t, std::uint32_t, 256> map;
  for (std::uint32_t i = 0; i < 200; ++i) {
    (void)map.try_emplace(i * 3u, i);
  }
  for (std::uint64_t n = 0; n < iterations; ++n) {
    metl_bench::do_not_optimize(map.find(static_cast<std::uint32_t>((n % 200) * 3u + 1u)));
  }
}

void crc32_1kib(std::uint64_t iterations) {
  std::uint8_t buffer[1024];
  for (std::size_t i = 0; i < sizeof buffer; ++i) {
    buffer[i] = static_cast<std::uint8_t>(i);
  }
  for (std::uint64_t n = 0; n < iterations; ++n) {
    metl_bench::do_not_optimize(metl::crc32(metl::span<const std::uint8_t>{buffer, sizeof buffer}));
  }
}

}  // namespace

int main(int argc, char** argv) {
  const auto cfg = metl_bench::config::from_args(argc, argv);

  metl_bench::header(cfg, "containers — bulk operations (per whole operation, not per element)");
  metl_bench::run("fixed_vector<64> fill + clear", cfg, fixed_vector_fill_clear);
  metl_bench::run("ring_buffer<64> push + pop", cfg, ring_buffer_push_pop);
  metl_bench::run("ring_buffer<64> push_overwrite", cfg, ring_buffer_push_overwrite);
  metl_bench::run("crc32 over 1 KiB", cfg, crc32_1kib);

  metl_bench::header(cfg, "lookup — hit and miss reported separately");
  metl_bench::run("flat_map<256> find (hit)", cfg, flat_map_find_hit);
  metl_bench::run("flat_map<256> find (miss)", cfg, flat_map_find_miss);
  metl_bench::run("static_unordered_map<256> find (hit)", cfg, unordered_map_find_hit);
  metl_bench::run("static_unordered_map<256> find (miss)", cfg, unordered_map_find_miss);

  return metl_bench::require_selection(cfg);
}
