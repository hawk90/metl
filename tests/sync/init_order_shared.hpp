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
