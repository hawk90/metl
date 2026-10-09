#include "intrusive_ptr_abi.hpp"

int take(metl::intrusive_ptr<abi_node> p) {
  return p ? p->value + static_cast<int>(p->use_count()) : -1;
}

metl::intrusive_ptr<abi_node> make(abi_node* raw) {
  return metl::intrusive_ptr<abi_node>(raw, metl::retain_ref);
}
