#include <metl/fixed_string.hpp>

int main() {
  metl::fixed_string<8> text;
  if (!text.empty() || text.capacity() != 8 || text.c_str()[0] != '\0') {
    return 1;
  }

  if (!text.assign("ab")) {
    return 2;
  }

  if (text.size() != 2 || text[0] != 'a' || text[1] != 'b') {
    return 3;
  }

  if (!text.try_push_back('c') || !text.append("de")) {
    return 4;
  }

  if (text.size() != 5 || text.back() != 'e') {
    return 5;
  }

  auto view = text.as_span();
  if (view.size() != 5 || view[2] != 'c') {
    return 6;
  }

  metl::fixed_string<8> same("abcde");
  if (text != same) {
    return 7;
  }

  if (!text.try_pop_back() || text.size() != 4 || text.c_str()[4] != '\0') {
    return 8;
  }

  if (!text.append("fghi")) {
    return 9;
  }

  if (!text.full() || text.c_str()[8] != '\0') {
    return 10;
  }

  if (text.append("yz")) {
    return 11;
  }

  if (text.size() != 8 || text.back() != 'i') {
    return 12;
  }

  text.clear();
  if (!text.empty() || text.c_str()[0] != '\0') {
    return 13;
  }

  return 0;
}
