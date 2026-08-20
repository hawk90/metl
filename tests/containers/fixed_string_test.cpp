#include <functional>
#include <unordered_map>

#include <metl/fixed_string.hpp>

int main() {
  metl::fixed_string<8> text;
  if (!text.empty() || text.capacity() != 8 || text.c_str()[0] != '\0') {
    return 1;
  }

  if (!text.try_assign("ab")) {
    return 2;
  }

  if (text.size() != 2 || text[0] != 'a' || text[1] != 'b') {
    return 3;
  }

  if (!text.try_push_back('c') || !text.try_append("de")) {
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

  if (!text.try_append("fghi")) {
    return 9;
  }

  if (!text.full() || text.c_str()[8] != '\0') {
    return 10;
  }

  if (text.try_append("yz")) {
    return 11;
  }

  if (text.size() != 8 || text.back() != 'i') {
    return 12;
  }

  text.clear();
  if (!text.empty() || text.c_str()[0] != '\0') {
    return 13;
  }

  // Constructing from a string that fits is fine (exact-capacity boundary).
  metl::fixed_string<5> exact("abcde");
  if (exact.size() != 5 || exact.back() != 'e') {
    return 14;
  }

  // assign() is the recoverable, non-asserting overflow path: it reports
  // failure via its bool result and leaves the string unchanged rather than
  // silently truncating. (The const char* constructor instead asserts on
  // overflow, which cannot be exercised in-process without aborting.)
  metl::fixed_string<3> small;
  if (small.try_assign("toolong")) {
    return 15;
  }
  if (!small.empty()) {
    return 16;
  }

  // Cross-capacity comparison: content is compared, not capacity. Equal
  // content in differing-capacity strings compares equal.
  metl::fixed_string<8> abc8("abc");
  metl::fixed_string<16> abc16("abc");
  if (!(abc8 == abc16) || abc8 != abc16) {
    return 17;
  }
  if (abc8 < abc16 || abc16 < abc8 || !(abc8 <= abc16) || !(abc8 >= abc16)) {
    return 18;
  }

  // Lexicographic ordering by byte content across capacities: "abc" < "abd".
  metl::fixed_string<16> abd16("abd");
  if (!(abc8 < abd16) || !(abc8 <= abd16) || abc8 >= abd16 || abc8 == abd16) {
    return 19;
  }
  if (!(abd16 > abc8) || !(abd16 >= abc8) || abd16 <= abc8) {
    return 20;
  }

  // Prefix ordering: a proper prefix is less than the longer string.
  metl::fixed_string<8> ab8("ab");
  metl::fixed_string<16> abc16b("abc");
  if (!(ab8 < abc16b) || abc16b < ab8 || ab8 == abc16b) {
    return 21;
  }

  // std::hash smoke: equal content -> equal hash (even across capacities),
  // and fixed_string is usable as a hash-map key type.
  std::hash<metl::fixed_string<8>> hash8;
  std::hash<metl::fixed_string<16>> hash16;
  if (hash8(abc8) != hash16(abc16)) {
    return 22;
  }
  if (hash8(abc8) == hash8(ab8)) {
    // Distinct content should (overwhelmingly) hash differently; a collision
    // here for such short inputs indicates a broken hasher.
    return 23;
  }

  std::unordered_map<metl::fixed_string<8>, int> keyed;
  keyed[abc8] = 42;
  keyed[ab8] = 7;
  if (keyed.size() != 2 || keyed[metl::fixed_string<8>("abc")] != 42 || keyed[ab8] != 7) {
    return 24;
  }

  return 0;
}
