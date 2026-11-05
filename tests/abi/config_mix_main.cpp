// The program tools/check_abi_tag.py links from two config_mix_reader.cpp
// objects: read_unchecked built at METL_HARDENING_NONE, read_checked at FAST.
// `unchecked` must return; `checked` must stop at the assert.

#include <cstring>

int read_unchecked(int index) noexcept;
int read_checked(int index) noexcept;

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "unchecked") == 0) {
    (void)read_unchecked(3);
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "checked") == 0) {
    (void)read_checked(3);
    return 0;  // reached only if the check is gone
  }
  return 2;
}
