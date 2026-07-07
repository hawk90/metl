// mmio_peripheral.cpp
//
// Driving a memory-mapped peripheral with METL's zero-cost register helpers:
//
//   - metl::mmio_ptr / metl::read_once / metl::write_once — volatile-correct,
//     un-reorderable, un-elidable register access.
//   - metl::bitfield — compile-time typed field mask/extract/insert so control
//     and status bits are named, not magic numbers.
//
// !!! There is NO real hardware here. The "peripheral" is a plain struct in
// !!! normal RAM that this program pokes to *simulate* device behaviour. On a
// !!! real MCU you would instead point mmio_ptr at the fixed peripheral base
// !!! address (e.g. 0x4001'3800 for USART1 on an STM32) and delete the
// !!! software model. The driver code below would be identical.
//
// Modeled peripheral: a tiny UART with three 32-bit registers.

#include <cstdint>
#include <cstdio>

#include <metl/bitfield.hpp>
#include <metl/mmio.hpp>

namespace {

// ---- Fake peripheral register block (stands in for real MMIO) --------------
struct uart_regs {
  volatile std::uint32_t CR;  // control
  volatile std::uint32_t SR;  // status
  volatile std::uint32_t DR;  // data
};

// ---- Typed bitfields for the control register ------------------------------
using cr_enable = metl::bitfield_u32<0, 1>;     // bit 0: peripheral enable
using cr_tx_en = metl::bitfield_u32<1, 1>;      // bit 1: transmitter enable
using cr_baud_div = metl::bitfield_u32<8, 16>;  // bits [23:8]: baud divisor

// ---- Typed bitfields for the status register -------------------------------
using sr_tx_empty = metl::bitfield_u32<0, 1>;  // bit 0: TX holding reg empty
using sr_rx_ready = metl::bitfield_u32<1, 1>;  // bit 1: RX data available

constexpr std::uint32_t kTxEmpty = sr_tx_empty::mask;
constexpr std::uint32_t kRxReady = sr_rx_ready::mask;

// A software stand-in for the wire: whatever the driver "transmits" lands here
// so the example can verify it. On real hardware the UART shifts it out a pin.
std::uint8_t g_tx_capture[8];
std::size_t g_tx_count = 0;

// Simulate the hardware side-effects of a register write. A real device does
// this in silicon; here we react to the driver's stores so the model behaves.
void hw_react(uart_regs& regs) {
  // If enabled + tx enabled and the driver wrote a byte, "transmit" it and
  // re-raise TX_EMPTY (hardware clears it on write, sets it when the shift
  // register drains).
  const bool enabled = cr_enable::extract(regs.CR) != 0 && cr_tx_en::extract(regs.CR) != 0;
  if (enabled && sr_tx_empty::extract(regs.SR) == 0) {
    if (g_tx_count < sizeof(g_tx_capture)) {
      g_tx_capture[g_tx_count++] = static_cast<std::uint8_t>(regs.DR & 0xFFu);
    }
    regs.SR |= kTxEmpty;  // shift register drained
  }
}

// ---- The driver: written exactly as it would be for real MMIO --------------
class uart_driver {
 public:
  explicit uart_driver(uart_regs* regs) noexcept
      : cr_(&regs->CR), sr_(&regs->SR), dr_(&regs->DR), regs_(regs) {}

  void configure(std::uint16_t baud_div) noexcept {
    // Read-modify-write the baud field without disturbing other bits.
    cr_.modify(cr_baud_div::mask, cr_baud_div::insert(0u, baud_div));
    // Enable the peripheral and transmitter via named single-bit fields.
    cr_.set_bits(cr_enable::mask | cr_tx_en::mask);
    // Start with an empty TX holding register.
    sr_.set_bits(kTxEmpty);
  }

  void write_byte(std::uint8_t byte) noexcept {
    // Poll until the transmit holding register is empty. read() goes through
    // read_once so the loop actually re-reads the register every iteration.
    while ((sr_.read() & kTxEmpty) == 0) {
      hw_react(*regs_);  // model advances; real HW does this on its own
    }
    sr_.clear_bits(kTxEmpty);  // writing data clears "empty"
    dr_.write(byte);
    hw_react(*regs_);  // let the model drain + re-raise TX_EMPTY
  }

  std::uint16_t baud_div() const noexcept {
    return static_cast<std::uint16_t>(cr_baud_div::extract(cr_.read()));
  }

 private:
  metl::mmio_ptr<std::uint32_t> cr_;
  metl::mmio_ptr<std::uint32_t> sr_;
  metl::mmio_ptr<std::uint32_t> dr_;
  uart_regs* regs_;  // only needed to run the software HW model
};

}  // namespace

int main() {
  uart_regs regs{};  // "peripheral" living in RAM
  uart_driver uart(&regs);

  uart.configure(/*baud_div=*/0x0139);  // 115200-ish divisor, arbitrary here
  if (uart.baud_div() != 0x0139) {
    return 1;
  }
  if (cr_enable::extract(regs.CR) == 0 || cr_tx_en::extract(regs.CR) == 0) {
    return 2;
  }

  const char msg[] = "METL";
  for (const char* p = msg; *p != '\0'; ++p) {
    uart.write_byte(static_cast<std::uint8_t>(*p));
  }

  // Verify the bytes reached the (fake) wire intact.
  if (g_tx_count != 4) {
    return 3;
  }
  for (std::size_t i = 0; i < g_tx_count; ++i) {
    if (g_tx_capture[i] != static_cast<std::uint8_t>(msg[i])) {
      return 4;
    }
  }

  // Also demonstrate the free-function primitives directly (no wrapper).
  metl::write_once<std::uint32_t>(&regs.DR, 0xABu);
  if (metl::read_once<std::uint32_t>(&regs.DR) != 0xABu) {
    return 5;
  }
  (void)kRxReady;  // documented above; unused in this TX-only demo

  std::printf("mmio_peripheral: sent \"%s\" (%zu bytes) at div=0x%04X\n",
              reinterpret_cast<const char*>(g_tx_capture),
              g_tx_count,
              static_cast<unsigned>(uart.baud_div()));
  return 0;
}
