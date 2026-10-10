// The queues of init_order_test, in their own translation unit as a driver's
// .cpp would hold them. METL_CONST_INIT turns "this is constant-initialized"
// into a compile error where it is not (clang in every mode, and constinit in
// C++20); the link order of the test makes the same failure visible at run time
// everywhere else.
#include "init_order_shared.hpp"

#include <metl/atomic_handle.hpp>
#include <metl/attributes.hpp>
#include <metl/handle_pool.hpp>
#include <metl/mpmc_queue.hpp>
#include <metl/spsc_byte_ring.hpp>
#include <metl/spsc_queue.hpp>

METL_CONST_INIT metl::spsc_queue<int, 8> g_spsc;
METL_CONST_INIT metl::spsc_byte_ring<16> g_ring;
METL_CONST_INIT metl::mpmc_queue<int, 8> g_mpmc;
METL_CONST_INIT metl::atomic_handle<init_order_handle> g_handle;
