// sensor_pipeline.cpp
//
// Zero-heap sensor pipeline:
//
//   producer thread  --(metl::spsc_queue)-->  consumer (main loop)
//                                                  |
//                                                  v
//                                       metl::ring_buffer (windowed avg)
//                                                  |
//                                                  v
//                                       metl::fixed_vector (batch)
//
// In an embedded target the producer would be an ISR posting samples to
// the SPSC queue; on the host we approximate that with an std::thread.
// metl::expected is used for the parse/validate stage.
//
// No heap allocation occurs in any of the metl containers used here.
// std::thread itself is the only host-side allocator dependency and is
// not part of the metl public API.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

#include <metl/expected.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/spsc_queue.hpp>

namespace {

struct sample {
  std::uint32_t seq;
  std::int32_t raw;  // ADC counts
};

enum class sensor_error : std::uint8_t {
  out_of_range,
  invalid_sequence,
};

constexpr std::size_t kQueueCapacity = 64;  // power of two — SPSC requirement
constexpr std::size_t kWindowCapacity = 8;  // moving average window
constexpr std::size_t kBatchCapacity = 16;  // batch flush size
constexpr std::size_t kSamplesToProduce = 200;

using sample_queue = metl::spsc_queue<sample, kQueueCapacity>;

// Validate one sample. Return scaled (millivolt) reading on success.
metl::expected<std::int32_t, sensor_error> validate(const sample& s, std::uint32_t expected_seq) noexcept {
  if (s.seq != expected_seq) {
    return metl::unexpected<sensor_error>(sensor_error::invalid_sequence);
  }
  if (s.raw < -32768 || s.raw > 32767) {
    return metl::unexpected<sensor_error>(sensor_error::out_of_range);
  }
  // Convert 16-bit signed ADC counts to integer millivolts. Range fits
  // comfortably in int32 so the multiplication never overflows.
  return static_cast<std::int32_t>(s.raw * 100);
}

}  // namespace

int main() {
  sample_queue q;
  std::atomic<bool> producer_done{false};

  // ---- Producer: simulated ISR generating a sawtooth signal ---------------
  std::thread producer([&] {
    for (std::uint32_t i = 0; i < kSamplesToProduce; ++i) {
      sample s{i, static_cast<std::int32_t>((i % 200) - 100)};
      // Back-pressure: spin until the queue accepts. The real ISR would
      // drop and increment a counter instead; we keep things simple here.
      while (!q.try_push(s)) {
        std::this_thread::yield();
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  // ---- Consumer: validate, push into the rolling window, batch ------------
  metl::ring_buffer<std::int32_t, kWindowCapacity> window;
  metl::fixed_vector<std::int32_t, kBatchCapacity> batch;

  std::uint32_t expected_seq = 0;
  unsigned out_of_range_count = 0;
  unsigned seq_error_count = 0;
  unsigned consumed = 0;
  std::int64_t running_sum = 0;
  unsigned batches_flushed = 0;
  std::int32_t last_avg = 0;

  while (true) {
    sample s{};
    if (!q.try_pop(s)) {
      if (producer_done.load(std::memory_order_acquire) && q.empty()) {
        break;
      }
      std::this_thread::yield();
      continue;
    }

    auto parsed = validate(s, expected_seq);
    ++expected_seq;
    if (!parsed) {
      if (parsed.error() == sensor_error::out_of_range) {
        ++out_of_range_count;
      } else {
        ++seq_error_count;
      }
      continue;
    }

    // Push into the rolling window (overwrites oldest on full).
    window.push_overwrite(*parsed);
    ++consumed;

    // Compute current windowed average.
    std::int64_t acc = 0;
    for (std::size_t i = 0; i < window.size(); ++i) {
      acc += window[i];
    }
    last_avg = static_cast<std::int32_t>(acc / static_cast<std::int64_t>(window.size()));

    // Accumulate into the batch; flush when full.
    if (!batch.try_push_back(last_avg)) {
      // Should not happen — we flush before push fails — but guard for safety.
      return 1;
    }
    running_sum += last_avg;

    if (batch.size() == kBatchCapacity) {
      ++batches_flushed;
      batch.clear();
    }
  }

  producer.join();

  // ---- Self-checks --------------------------------------------------------
  if (consumed != kSamplesToProduce) {
    std::fprintf(stderr, "sensor_pipeline: consumed=%u expected=%zu\n", consumed, kSamplesToProduce);
    return 2;
  }
  if (out_of_range_count != 0 || seq_error_count != 0) {
    std::fprintf(
        stderr, "sensor_pipeline: unexpected errors oor=%u seq=%u\n", out_of_range_count, seq_error_count);
    return 3;
  }
  if (window.size() != kWindowCapacity) {
    return 4;
  }
  if (batches_flushed == 0) {
    return 5;
  }

  std::printf("sensor_pipeline: consumed=%u batches=%u window=%zu running_sum=%lld last_avg=%d\n",
              consumed,
              batches_flushed,
              window.size(),
              static_cast<long long>(running_sum),
              last_avg);
  return 0;
}
