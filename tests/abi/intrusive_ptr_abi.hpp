// Passed across a compiler boundary: tests/abi/intrusive_ptr_lib.cpp is built
// by one compiler, intrusive_ptr_main.cpp by the other, and the `mixed-abi`
// CI job links them. With [[clang::trivial_abi]] on by default, Clang passed
// intrusive_ptr in a register and GCC in memory, and this crashed both ways.
#pragma once

#include <metl/intrusive_ptr.hpp>

struct abi_node final : metl::intrusive_ref_counter<abi_node, metl::refcount_kind::non_atomic> {
  int value = 0;
};

int take(metl::intrusive_ptr<abi_node> p);
metl::intrusive_ptr<abi_node> make(abi_node* raw);
