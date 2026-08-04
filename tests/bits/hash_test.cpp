#include <metl/hash.hpp>

int main() {
  // FNV-1a of the same bytes yields the same hash; different bytes yield different hashes.
  const char a[] = "hello";
  const char b[] = "hello";
  const char c[] = "world";

  std::size_t ha = metl::fnv1a(a, sizeof(a) - 1);
  std::size_t hb = metl::fnv1a(b, sizeof(b) - 1);
  std::size_t hc = metl::fnv1a(c, sizeof(c) - 1);

  if (ha != hb) {
    return 1;
  }
  if (ha == hc) {
    return 2;
  }

  // Empty input returns the offset basis (non-zero).
  if (metl::fnv1a(a, 0) == 0) {
    return 3;
  }

  // hash_combine is non-commutative: combine(a, b) != combine(b, a) for distinct values.
  std::size_t ab = metl::hash_combine(1, 2);
  std::size_t ba = metl::hash_combine(2, 1);
  if (ab == ba) {
    return 4;
  }

  // hash_combine_all empty pack returns 0.
  if (metl::hash_combine_all() != 0) {
    return 5;
  }

  // hash_combine_all consistency.
  std::size_t pack1 = metl::hash_combine_all(1, 2, 3);
  std::size_t pack2 = metl::hash_combine_all(1, 2, 3);
  std::size_t pack3 = metl::hash_combine_all(3, 2, 1);
  if (pack1 != pack2) {
    return 6;
  }
  if (pack1 == pack3) {
    return 7;
  }

  // identity_hash is the value itself cast to size_t.
  metl::identity_hash ih;
  if (ih(42) != static_cast<std::size_t>(42)) {
    return 8;
  }
  if (ih(static_cast<unsigned char>(7)) != static_cast<std::size_t>(7)) {
    return 9;
  }

  // fnv1a_hash is deterministic.
  metl::fnv1a_hash fh;
  int x = 12345;
  int y = 12345;
  int z = 99999;
  if (fh(x) != fh(y)) {
    return 10;
  }
  if (fh(x) == fh(z)) {
    return 11;
  }

  // is_transparent typedef present on both.
  using id_transparent_t = metl::identity_hash::is_transparent;
  using fnv_transparent_t = metl::fnv1a_hash::is_transparent;
  (void)static_cast<id_transparent_t*>(nullptr);
  (void)static_cast<fnv_transparent_t*>(nullptr);

  // fnv1a_hash handles C strings via overload.
  if (fh("alpha") == fh("beta")) {
    return 12;
  }
  if (fh("alpha") != fh("alpha")) {
    return 13;
  }

  // fnv1a of an unsigned char buffer is usable at compile time.
  constexpr unsigned char ce_buf[3] = {1, 2, 3};
  constexpr std::size_t ce_hash = metl::fnv1a(ce_buf, 3);
  static_assert(ce_hash != 0, "fnv1a must be constexpr-evaluable for byte inputs");

  return 0;
}
