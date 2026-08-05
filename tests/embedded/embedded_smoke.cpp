// Embedded freestanding compile check.
//
// Includes every public METL header and forces instantiation of at least
// one representative type per header. No main(): this translation unit is
// archived into a static library so the toolchain only validates that the
// library compiles cleanly under freestanding ARM (Cortex-M) -- no linker
// is involved beyond the archiver.

// ---- Every public header ---------------------------------------------------
#include "metl/arena_allocator.hpp"
#include "metl/assert.hpp"
#include "metl/atomic_ref.hpp"
#include "metl/bit.hpp"
#include "metl/bitfield.hpp"
#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/crc16.hpp"
#include "metl/crc32.hpp"
#include "metl/crc8.hpp"
#include "metl/delegate.hpp"
#include "metl/endian.hpp"
#include "metl/event_dispatcher.hpp"
#include "metl/expected.hpp"
#include "metl/fixed_deque.hpp"
#include "metl/fixed_function.hpp"
#include "metl/fixed_queue.hpp"
#include "metl/fixed_stack.hpp"
#include "metl/fixed_string.hpp"
#include "metl/fixed_vector.hpp"
#include "metl/flat_map.hpp"
#include "metl/flat_set.hpp"
#include "metl/fsm.hpp"
#include "metl/function_ref.hpp"
#include "metl/hash.hpp"
#include "metl/in_place.hpp"
#include "metl/intrusive_ptr.hpp"
#include "metl/lookup_table.hpp"
#include "metl/metl.hpp"
#include "metl/mmio.hpp"
#include "metl/monotonic_buffer.hpp"
#include "metl/object_pool.hpp"
#include "metl/optional.hpp"
#include "metl/register_access.hpp"
#include "metl/ring_buffer.hpp"
#include "metl/scope_exit.hpp"
#include "metl/span.hpp"
#include "metl/spsc_queue.hpp"
#include "metl/static_allocator.hpp"
#include "metl/static_message_queue.hpp"
#include "metl/static_unordered_map.hpp"
#include "metl/static_unordered_set.hpp"
#include "metl/type_traits.hpp"
#include "metl/variant.hpp"
#include "metl/version.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

// ---- Helpers --------------------------------------------------------------

struct dummy_refcounted final : metl::intrusive_ref_counter<dummy_refcounted> {};

enum class smoke_state : std::uint8_t { off, on };
enum class smoke_event : std::uint8_t { toggle };

// MMIO targets must bind against real volatile storage so the compile check
// also exercises the read_once/write_once volatile codegen path.
volatile std::uint32_t g_smoke_mmio_reg{};

// Source containers backing span / static_allocator instantiations.
std::array<int, 4> g_smoke_int_buf{};

// Atomic refs require an externally-owned, suitably-aligned trivially
// copyable object. Bind against a real uint32_t.
alignas(std::atomic<std::uint32_t>) std::uint32_t g_smoke_atomic_storage{};

// ---- Forced instantiations (one+ per header) -------------------------------

// arena_allocator.hpp
[[maybe_unused]] metl::arena_allocator<64> _arena;

// assert.hpp -- function-level. Take a pointer to ensure the symbol is used.
[[maybe_unused]] const auto _assert_handler_ptr = &metl::set_assert_handler;
[[maybe_unused]] const auto _panic_handler_ptr = &metl::set_panic_handler;

// atomic_ref.hpp
[[maybe_unused]] metl::atomic_ref<std::uint32_t> _atomic_ref(g_smoke_atomic_storage);

// bit.hpp -- evaluate at constant context to force instantiation.
static_assert(metl::popcount(std::uint32_t{0xFFu}) == 8, "bit.hpp popcount");

// bitfield.hpp -- lsb=0, width=4, default uint32_t carrier.
using smoke_bitfield = metl::bitfield<0, 4>;
static_assert(smoke_bitfield::width == 4, "bitfield width");

// compiler.hpp -- exercise inline constexpr values.
static_assert(metl::cxx_standard >= 201703L, "compiler.hpp cxx_standard");

// config.hpp -- inline constexpr.
static_assert(metl::version_major == 0, "config.hpp version_major");

// crc8/16/32.hpp -- exercise template instantiation; bind results to ODR-used
// inline constexprs so the compiler emits the functions and walks the body.
inline constexpr auto _crc8_empty = metl::crc8(metl::span<const std::uint8_t>{});
inline constexpr auto _crc16_empty = metl::crc16(metl::span<const std::uint8_t>{});
inline constexpr auto _crc32_empty = metl::crc32(metl::span<const std::uint8_t>{});
static_assert(_crc8_empty == _crc8_empty, "crc8 instantiated");
static_assert(_crc16_empty == _crc16_empty, "crc16 instantiated");
static_assert(_crc32_empty == _crc32_empty, "crc32 instantiated");

// delegate.hpp
[[maybe_unused]] metl::delegate<int(int)> _delegate;

// endian.hpp -- byteswap function.
static_assert(metl::byteswap(std::uint16_t{0x1234}) == std::uint16_t{0x3412}, "endian byteswap");

// event_dispatcher.hpp -- signature + capacity.
[[maybe_unused]] metl::event_dispatcher<void(int), 4> _dispatcher;

// expected.hpp -- both value and void specializations.
[[maybe_unused]] metl::expected<int, int> _expected_int{1};
[[maybe_unused]] metl::expected<void, int> _expected_void;

// fixed_deque.hpp
[[maybe_unused]] metl::fixed_deque<int, 4> _fdeque;

// fixed_function.hpp
[[maybe_unused]] metl::fixed_function<int(int), 32> _ffunc;

// fixed_queue.hpp
[[maybe_unused]] metl::fixed_queue<int, 4> _fqueue;

// fixed_stack.hpp
[[maybe_unused]] metl::fixed_stack<int, 4> _fstack;

// fixed_string.hpp
[[maybe_unused]] metl::fixed_string<16> _fstr;

// fixed_vector.hpp
[[maybe_unused]] metl::fixed_vector<int, 4> _fvec;

// flat_map.hpp
[[maybe_unused]] metl::flat_map<int, int, 4> _fmap;

// flat_set.hpp
[[maybe_unused]] metl::flat_set<int, 4> _fset;

// fsm.hpp -- minimal one-transition FSM.
constexpr std::array<metl::fsm_transition<smoke_state, smoke_event>, 1> g_smoke_fsm_transitions{{
    metl::fsm_transition<smoke_state, smoke_event>{
        smoke_state::off, smoke_event::toggle, smoke_state::on, {}},
}};
[[maybe_unused]] metl::fsm<smoke_state, smoke_event, 1> _fsm{smoke_state::off, g_smoke_fsm_transitions};

// function_ref.hpp
int _smoke_fref_fn(int x) noexcept {
  return x;
}
[[maybe_unused]] metl::function_ref<int(int)> _fref{&_smoke_fref_fn};

// hash.hpp
[[maybe_unused]] metl::fnv1a_hash _hash_fnv;
[[maybe_unused]] metl::identity_hash _hash_id;
static_assert(metl::fnv1a(static_cast<const unsigned char*>(nullptr), 0) ==
                  metl::fnv1a(static_cast<const unsigned char*>(nullptr), 0),
              "hash.hpp fnv1a");

// in_place.hpp -- tag types.
[[maybe_unused]] metl::in_place_t _ip{};
[[maybe_unused]] metl::in_place_type_t<int> _ipt{};
[[maybe_unused]] metl::in_place_index_t<0> _ipi{};
[[maybe_unused]] const metl::nullopt_t& _nopt_ref = metl::nullopt;
[[maybe_unused]] metl::monostate _mono;

// intrusive_ptr.hpp -- default-constructed null pointer plus ref-counter base.
[[maybe_unused]] metl::intrusive_ptr<dummy_refcounted> _iptr;

// lookup_table.hpp -- a single-entry table.
constexpr std::array<metl::lookup_entry<int, int>, 1> g_smoke_lookup{{{1, 100}}};
[[maybe_unused]] metl::lookup_table<int, int, 1> _lut{g_smoke_lookup};

// metl.hpp -- umbrella header (no additional instantiation needed).

// mmio.hpp -- compile-time-address register at a synthetic address.
using smoke_mmio_reg = metl::mmio_register<std::uint32_t, 0x4000'1000u>;
static_assert(smoke_mmio_reg::address == 0x4000'1000u, "mmio_register address");
[[maybe_unused]] metl::mmio_ptr<std::uint32_t> _mmio_ptr{&g_smoke_mmio_reg};

// monotonic_buffer.hpp
[[maybe_unused]] metl::monotonic_buffer<128> _mbuf;

// object_pool.hpp
[[maybe_unused]] metl::object_pool<int, 4> _opool;

// optional.hpp
[[maybe_unused]] metl::optional<int> _opt;

// register_access.hpp -- read_once / write_once bound to volatile storage.
inline void _smoke_register_access_use() noexcept {
  metl::write_once<std::uint32_t>(&g_smoke_mmio_reg, 0u);
  (void)metl::read_once<std::uint32_t>(&g_smoke_mmio_reg);
  metl::barrier_full();
  metl::barrier_acquire();
  metl::barrier_release();
}
[[maybe_unused]] const auto _register_access_use_ptr = &_smoke_register_access_use;

// ring_buffer.hpp
[[maybe_unused]] metl::ring_buffer<int, 4> _ring;

// scope_exit.hpp -- factory keeps the lambda type alive for the TU.
inline auto _smoke_make_scope_exit() noexcept {
  return metl::make_scope_exit([]() noexcept {});
}
[[maybe_unused]] const auto _scope_exit_factory_ptr = &_smoke_make_scope_exit;

// span.hpp
[[maybe_unused]] metl::span<int, 4> _span_static{g_smoke_int_buf};
[[maybe_unused]] metl::span<int> _span_dyn{g_smoke_int_buf.data(), g_smoke_int_buf.size()};

// spsc_queue.hpp
[[maybe_unused]] metl::spsc_queue<int, 4> _spsc;

// static_allocator.hpp
[[maybe_unused]] metl::static_allocator<int, 4> _salloc;

// static_message_queue.hpp
[[maybe_unused]] metl::static_message_queue<int, 4> _smqueue;

// static_unordered_map.hpp
[[maybe_unused]] metl::static_unordered_map<int, int, 8> _sumap;

// static_unordered_set.hpp
[[maybe_unused]] metl::static_unordered_set<int, 8> _suset;

// type_traits.hpp
[[maybe_unused]] metl::storage_for<int> _storage_int{};
static_assert(sizeof(metl::storage_for<int>) >= sizeof(int), "type_traits.hpp storage_for");

// variant.hpp
[[maybe_unused]] metl::variant<int, char> _variant{0};

// version.hpp
static_assert(metl::version::major == 0, "version.hpp major");

}  // namespace

// Single exported symbol: gives the archiver something to package and lets
// downstream link tests confirm the TU was actually compiled.
extern "C" void metl_embedded_smoke_anchor(void) noexcept {}
