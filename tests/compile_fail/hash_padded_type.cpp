// EXPECT-ERROR: fnv1a_hash default overload hashes the raw object representation
//
// The default overload hashes the BYTES. For a type with padding, two objects
// that compare equal can hold different bytes in the padding and hash
// differently -- which breaks the invariant every hash container is built on:
// `a == b` implies `hash(a) == hash(b)`. The failure is a lookup that misses an
// element the container is holding, at a rate that depends on what was in the
// padding, so it is intermittent and unreproducible.

#include <cstdint>

#include <metl/hash.hpp>

namespace {

struct packed {
  std::uint32_t a;
  std::uint32_t b;
};

struct padded {
  std::uint8_t a;
  // three bytes of padding here
  std::uint32_t b;
};

}  // namespace

std::size_t control(const packed& value) {
  return metl::fnv1a_hash{}(value);
}

#ifdef METL_COMPILE_FAIL
std::size_t offender(const padded& value) {
  return metl::fnv1a_hash{}(value);
}
#endif
