#pragma once

#include <metl/atomic_handle.hpp>
#include <metl/spsc_byte_ring.hpp>
#include <metl/spsc_queue.hpp>
#include <metl/versioned_handle.hpp>

struct init_order_tag {};
using init_order_handle = metl::versioned_handle<init_order_tag>;

extern metl::spsc_queue<int, 8> g_spsc;
extern metl::spsc_byte_ring<16> g_ring;
extern metl::atomic_handle<init_order_handle> g_handle;

// Since C++20 the containers whose empty state is all zero bytes are
// constant-initialized too (detail/constant_init.hpp): a C++20 constexpr
// constructor may leave the element buffer to its body, which zeroes it only
// during constant evaluation. In C++17 they stay dynamically initialized.
#if defined(__cpp_constinit)
#define INIT_ORDER_CONTAINERS 1
#include <metl/fixed_vector.hpp>
#include <metl/lock.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/static_message_queue.hpp>

extern metl::fixed_vector<int, 8> g_vector;
extern metl::ring_buffer<int, 8> g_ring_buffer;
extern metl::guarded<metl::static_message_queue<int, 8>, metl::null_lock> g_messages;
#else
#define INIT_ORDER_CONTAINERS 0
#endif
