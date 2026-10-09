// Comparing a fixed_string with a string literal.
//
// There was no const char* overload, so the literal went through the implicit
// fixed_string(const char*) constructor, which asserts when the literal does
// not fit: `fixed_string<4>("ab") == "abcdefgh"` aborted instead of returning
// false. The comparisons now read the literal in place.

#include "metl_check.hpp"

#include <metl/fixed_string.hpp>

namespace {

void longer_literal_compares_instead_of_aborting() {
  const metl::fixed_string<4> s("ab");
  CHECK(!(s == "abcdefgh"));
  CHECK(!("abcdefgh" == s));
  CHECK(s != "abcdefgh");
  CHECK("abcdefgh" != s);
  CHECK(s < "abcdefgh");  // "ab" is a prefix
  CHECK("abcdefgh" > s);
}

void equality_and_prefixes() {
  const metl::fixed_string<8> s("abc");
  CHECK(s == "abc");
  CHECK("abc" == s);
  CHECK(!(s == "ab"));    // text is a prefix of s
  CHECK(!(s == "abcd"));  // s is a prefix of text
  CHECK(!(s == ""));
  CHECK(metl::fixed_string<8>() == "");
}

void ordering_matches_fixed_string_ordering() {
  // Every pair, both directions, against the fixed_string-to-fixed_string
  // operators, which order as unsigned char.
  const char* const texts[] = {"", "a", "ab", "abc", "abd", "b", "\xff", "a\xff"};
  for (const char* left : texts) {
    const metl::fixed_string<8> l(left);
    for (const char* right : texts) {
      const metl::fixed_string<8> r(right);
      CHECK_EQ(l == right, l == r);
      CHECK_EQ(l != right, l != r);
      CHECK_EQ(l < right, l < r);
      CHECK_EQ(l > right, l > r);
      CHECK_EQ(l <= right, l <= r);
      CHECK_EQ(l >= right, l >= r);
      CHECK_EQ(right < l, r < l);
      CHECK_EQ(right == l, r == l);
    }
  }
}

}  // namespace

int main() {
  longer_literal_compares_instead_of_aborting();
  equality_and_prefixes();
  ordering_matches_fixed_string_ordering();
  return metl_test::exit_code();
}
