// Regression tests for hashing by value rather than by object bytes
// (docs/AUDIT.md, Section G.2).
//
// fnv1a_hash is transparent, so a lookup may hash a key of a different type
// from the stored one. Every case here is a pair of EQUAL values that used to
// hash differently -- which is a silent `find` miss, not a slow lookup.

#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>

#include <metl/fixed_string.hpp>
#include <metl/hash.hpp>
#include <metl/span.hpp>
#include <metl/static_unordered_map.hpp>

namespace {

enum class color : std::uint8_t { red = 3 };

}  // namespace

int main() {
  const metl::fnv1a_hash h;

  // fnv1a(const T*, len): `len` counts elements, not bytes. Two buffers that
  // differ only in their second element used to hash equally.
  {
    const std::uint32_t a[2] = {1, 2};
    const std::uint32_t b[2] = {1, 99};
    CHECK(metl::fnv1a(a, 2) != metl::fnv1a(b, 2));
    CHECK_EQ(metl::fnv1a(a, 2), metl::fnv1a(reinterpret_cast<const unsigned char*>(a), sizeof(a)));
  }

  // fixed_string keeps stale bytes past its terminator after a shrinking
  // assign or clear; those must not reach the hash.
  {
    metl::fixed_string<7> plain("ab");
    metl::fixed_string<7> shrunk("abcdef");
    shrunk.assign("ab");
    metl::fixed_string<7> rebuilt("abcdef");
    rebuilt.clear();
    rebuilt.push_back('a');
    rebuilt.push_back('b');

    CHECK(plain == shrunk);
    CHECK_EQ(h(plain), h(shrunk));
    CHECK_EQ(h(plain), h(rebuilt));

    metl::static_unordered_map<metl::fixed_string<7>, int, 8, metl::fnv1a_hash> map;
    map.emplace(plain, 1);
    CHECK(map.find(shrunk) != nullptr);
    CHECK(map.find(rebuilt) != nullptr);
  }

  // Character ranges of every kind interoperate: capacity does not matter,
  // and a C string or span hashes the same characters.
  {
    const metl::fixed_string<7> small("abc");
    const metl::fixed_string<32> large("abc");
    const char text[] = "abc";
    const metl::span<const char> chars(text, 3);
    CHECK_EQ(h(small), h(large));
    CHECK_EQ(h(small), h("abc"));
    CHECK_EQ(h(small), h(chars));

    metl::static_unordered_map<metl::fixed_string<7>, int, 8, metl::fnv1a_hash, std::equal_to<>> map;
    map.emplace(small, 7);
    const int* found = map.find("abc");
    CHECK(found != nullptr);
    CHECK(found != nullptr && *found == 7);
  }

  // A mutable `char*` and a non-const `char[N]` hash the same characters as a
  // `const char*`. The `char*` used to bind the template and hash the pointer's
  // address; the array hashed all N bytes, including what follows the NUL.
  {
    char buffer[32] = "abc";
    char* mutable_ptr = buffer;
    CHECK_EQ(h(mutable_ptr), h("abc"));
    CHECK_EQ(h(buffer), h("abc"));

    char unterminated[3] = {'a', 'b', 'c'};
    CHECK_EQ(h(unterminated), h("abc"));  // bounded by N, not by a NUL

    metl::static_unordered_map<metl::fixed_string<7>, int, 8, metl::fnv1a_hash, std::equal_to<>> map;
    map.emplace(metl::fixed_string<7>("abc"), 1);
    CHECK(map.find(mutable_ptr) != nullptr);
  }

  // Integers hash by value, whatever their width.
  {
    CHECK_EQ(h(5), h(5L));
    CHECK_EQ(h(5), h(5LL));
    CHECK_EQ(h(5), h(std::uint8_t{5}));
    CHECK_EQ(h(5), h(5U));
    CHECK_EQ(h(-1), h(static_cast<std::int8_t>(-1)));
    CHECK_EQ(h(-1), h(-1LL));
    CHECK(h(1) != h(2));
    CHECK_EQ(h(color::red), h(3));

    metl::static_unordered_map<long, int, 8, metl::fnv1a_hash, std::equal_to<>> map;
    map.emplace(5L, 50);
    const int* found = map.find(5);
    CHECK(found != nullptr);
  }

  return metl_test::exit_code();
}
