// spsc_isr.cpp
//
// metl::spsc_queue is a wait-free single-producer / single-consumer queue.
// Its canonical embedded use is handing data from an interrupt service
// routine (the producer) to the main loop / a lower-priority task (the
// consumer) with no locks and no disabled interrupts:
//
//     ISR context                         main-loop context
//     -----------                         -----------------
//     sample = read_adc();                while (q.try_pop(s)) {
//     q.try_push(sample);   ---- q ---->      process(s);
//     // never blocks, never              }
//     // allocates
//
// Rules that make this safe:
//   * EXACTLY ONE producer calls try_push/try_emplace, EXACTLY ONE consumer
//     calls try_pop. (Here that is ISR vs main loop.)
//   * Capacity must be a power of two.
//   * The queue holds Capacity-1 usable slots (one slot separates full/empty).
//   * On a full queue try_push returns false — the ISR must decide to drop the
//     sample (and bump an overrun counter) rather than block.
//
// To keep this example deterministic and dependency-free we drive the producer
// and consumer by hand in a single thread; the comments mark which half would
// run in the ISR and which in the main loop. See examples/sensor_pipeline.cpp
// for the same pattern exercised across two real std::threads (TSAN-checked).

#include <cstdint>
#include <cstdio>

#include <metl/spsc_queue.hpp>

namespace {

struct adc_sample {
  std::uint32_t seq;
  std::uint16_t value;
};

constexpr std::size_t kCapacity = 8;  // power of two -> 7 usable slots
using sample_queue = metl::spsc_queue<adc_sample, kCapacity>;

// Called "from the ISR": push one sample, drop + count on overrun. Never
// blocks. Returns false if the sample was dropped.
bool isr_produce(sample_queue& q, adc_sample s, unsigned& overruns) {
  if (!q.try_push(s)) {
    ++overruns;  // consumer fell behind; keep the newest by dropping this one
    return false;
  }
  return true;
}

}  // namespace

int main() {
  sample_queue q;
  unsigned overruns = 0;
  unsigned produced = 0;
  unsigned consumed = 0;
  std::uint64_t checksum = 0;

  std::uint32_t seq = 0;
  for (int round = 0; round < 20; ++round) {
    // ---- ISR half: burst of 5 acquisitions ---------------------------------
    for (int i = 0; i < 5; ++i) {
      adc_sample s{seq, static_cast<std::uint16_t>(seq * 3u)};
      if (isr_produce(q, s, overruns)) {
        ++produced;
      }
      ++seq;
    }

    // ---- main-loop half: drain whatever is available -----------------------
    adc_sample out{};
    while (q.try_pop(out)) {
      checksum += out.value;
      ++consumed;
    }
  }

  // Drain any tail left in the queue.
  adc_sample out{};
  while (q.try_pop(out)) {
    checksum += out.value;
    ++consumed;
  }

  // ---- Self-checks ----------------------------------------------------------
  // Every sample that was accepted must have been consumed exactly once.
  if (consumed != produced) {
    std::fprintf(stderr, "spsc_isr: consumed=%u != produced=%u\n", consumed, produced);
    return 1;
  }
  // The consumer drains every round, so with a 7-slot queue and 5-sample bursts
  // nothing should ever be dropped here.
  if (overruns != 0) {
    std::fprintf(stderr, "spsc_isr: unexpected overruns=%u\n", overruns);
    return 2;
  }
  if (q.capacity() != kCapacity || !q.empty()) {
    return 3;
  }

  std::printf("spsc_isr: produced=%u consumed=%u overruns=%u checksum=%llu\n",
              produced,
              consumed,
              overruns,
              static_cast<unsigned long long>(checksum));
  return 0;
}
