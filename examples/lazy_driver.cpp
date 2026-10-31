// lazy_driver.cpp
//
// A driver built on first use, which its own data-ready interrupt can reach
// before that first use has finished: metl::once_flag and metl::try_call_once.
//
// The driver here is an accelerometer that must be calibrated before its
// samples mean anything. Calibration reads a burst of samples to measure the
// zero-g offset, so it is slow, and it is wanted only if the application uses
// the sensor at all. The obvious code is a function-local static:
//
//     accel_driver& accel() { static accel_driver d; return d; }
//
// and that is the case that breaks. The interrupt is enabled before the
// calibration's burst ends, so it can call accel() while the constructor is
// still running. With -fno-threadsafe-statics the driver is then built twice;
// with thread-safe statics, a bare-metal __cxa_guard_acquire traps
// (docs/SCOPE.md section 5, measured under QEMU).
//
// try_call_once never waits and never masks the interrupt around the
// initializer. The main loop starts calibration; an interrupt that arrives
// meanwhile finds the flag not done and drops its sample instead of
// re-entering. Once the flag is done, everything the initializer wrote is
// visible to the interrupt (an acquire load against the initializer's release).
//
// The interrupt is modelled by calling the handler by hand, as spsc_isr.cpp
// does, including once from inside the calibration; the example is
// deterministic and returns non-zero if any of that goes wrong.

#include <cstdint>
#include <cstdio>

#include <metl/once_flag.hpp>
#include <metl/optional.hpp>

namespace {

// --- the fake peripheral -----------------------------------------------------

constexpr std::int16_t kZeroGOffset = 37;  // what calibration should find
std::uint32_t g_raw_reads = 0;

std::int16_t read_raw_sample() {
  ++g_raw_reads;
  // A sensor at rest: the offset plus a little alternating noise.
  return static_cast<std::int16_t>(kZeroGOffset + ((g_raw_reads % 2u) != 0u ? 1 : -1));
}

void accel_data_ready_isr();

// --- the driver ----------------------------------------------------------------

class accel_driver {
 public:
  // Calibrates: averages a burst of samples at rest. The data-ready interrupt
  // is already enabled, so it fires partway through.
  accel_driver() {
    std::int32_t sum = 0;
    for (int i = 0; i < kCalibrationSamples; ++i) {
      sum += read_raw_sample();
      if (i == kCalibrationSamples / 2) {
        accel_data_ready_isr();  // the interrupt arriving mid-calibration
      }
    }
    offset_ = static_cast<std::int16_t>((sum + kCalibrationSamples / 2) / kCalibrationSamples);  // rounded
  }

  std::int16_t corrected(std::int16_t raw) const { return static_cast<std::int16_t>(raw - offset_); }
  std::int16_t offset() const { return offset_; }

 private:
  static constexpr int kCalibrationSamples = 8;
  std::int16_t offset_ = 0;
};

// Both constant-initialized: nothing runs before main, and nothing is built
// until the first try_call_once.
METL_CONST_INIT metl::once_flag<> g_accel_once;
METL_CONST_INIT metl::optional<accel_driver> g_accel;

unsigned g_constructions = 0;
unsigned g_dropped = 0;    // samples the interrupt dropped: driver not ready
unsigned g_processed = 0;  // samples it corrected
std::int32_t g_corrected_sum = 0;

// Main-loop side: starts the driver if no one has. True once it is ready.
bool accel_ready() {
  return metl::try_call_once(g_accel_once, [] {
    ++g_constructions;
    g_accel.emplace();
  });
}

// Interrupt side: never builds the driver, never waits for it.
void accel_data_ready_isr() {
  const std::int16_t raw = read_raw_sample();
  if (!g_accel_once.is_done()) {
    ++g_dropped;  // not calibrated yet: this sample means nothing
    return;
  }
  g_corrected_sum += g_accel->corrected(raw);
  ++g_processed;
}

}  // namespace

int main() {
  // An interrupt before anyone asked for the driver: dropped, and it does not
  // start the calibration on its own.
  accel_data_ready_isr();

  // The main loop's first use: calibrates, and the interrupt that fires
  // inside the calibration is dropped too.
  if (!accel_ready()) {
    std::fprintf(stderr, "lazy_driver: first try_call_once did not finish\n");
    return 1;
  }

  // From here on every interrupt is processed.
  for (int i = 0; i < 10; ++i) {
    accel_data_ready_isr();
  }

  // A later call from the main loop finds the driver ready and runs nothing.
  if (!accel_ready()) {
    return 2;
  }

  if (g_constructions != 1) {
    std::fprintf(stderr, "lazy_driver: constructed %u times\n", g_constructions);
    return 3;
  }
  if (g_dropped != 2 || g_processed != 10) {
    std::fprintf(stderr, "lazy_driver: dropped=%u processed=%u\n", g_dropped, g_processed);
    return 4;
  }
  if (g_accel->offset() != kZeroGOffset) {
    std::fprintf(stderr, "lazy_driver: offset=%d\n", g_accel->offset());
    return 5;
  }
  // At rest, corrected samples are +1 / -1 noise around zero.
  if (g_corrected_sum < -10 || g_corrected_sum > 10) {
    std::fprintf(stderr, "lazy_driver: corrected sum=%ld\n", static_cast<long>(g_corrected_sum));
    return 6;
  }

  std::printf("lazy_driver: constructed=%u dropped=%u processed=%u offset=%d\n",
              g_constructions,
              g_dropped,
              g_processed,
              g_accel->offset());
  return 0;
}
