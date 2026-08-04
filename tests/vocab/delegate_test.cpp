#include <metl/delegate.hpp>

namespace {

int add_three(int value) {
  return value + 3;
}

struct device {
  int scale(int value) { return factor * value; }
  int shift(int value) const { return offset + value; }

  int factor;
  int offset;
};

}  // namespace

int main() {
  auto free_delegate = metl::delegate<int(int)>::from_function<&add_three>();
  if (!free_delegate || free_delegate(4) != 7) {
    return 1;
  }

  device state{5, 9};
  auto member_delegate = metl::delegate<int(int)>::bind<device, &device::scale>(state);
  if (!member_delegate || member_delegate(3) != 15) {
    return 2;
  }

  auto const_delegate = metl::delegate<int(int)>::bind<device, &device::shift>(state);
  if (!const_delegate || const_delegate(4) != 13) {
    return 3;
  }

  state.factor = 2;
  state.offset = 1;

  if (member_delegate(6) != 12 || const_delegate(8) != 9) {
    return 4;
  }

  metl::delegate<int(int)> empty;
  if (empty.has_value()) {
    return 5;
  }

  return 0;
}
