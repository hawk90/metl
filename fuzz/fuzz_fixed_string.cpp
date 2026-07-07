// libFuzzer harness for metl::fixed_string.
//
// Drives a fixed-capacity string with an opcode stream of CONTRACT-VALID
// operations only (try_push_back / try_pop_back / assign / append / clear plus
// bounded reads). Overflowing operations that would assert-abort are never
// invoked; assign/append/try_* report overflow via their bool result instead.
// Round-trip invariants (assign then c_str()/size()) are checked so a mismatch
// surfaces as a real bug, and every buffer touch is ASan/UBSan-instrumented.

#include "fuzz_helpers.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <metl/fixed_string.hpp>
#include <metl/span.hpp>

namespace {

constexpr std::size_t kCapacity = 64;

void check_null_terminated(const metl::fixed_string<kCapacity>& s) {
  // c_str() must carry a readable NUL terminator exactly at size(). Note the
  // buffer MAY contain embedded NULs (try_push_back accepts any char), so
  // strlen() can be shorter than size() — it can never legitimately exceed it.
  if (s.c_str()[s.size()] != '\0') {
    __builtin_trap();
  }
  if (std::strlen(s.c_str()) > s.size()) {
    __builtin_trap();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  metl_fuzz::byte_reader in(data, size);
  metl::fixed_string<kCapacity> str;

  while (!in.empty()) {
    switch (in.byte() % 8u) {
      case 0: {  // try_push_back — never asserts (returns false when full)
        str.try_push_back(static_cast<char>(in.byte()));
        break;
      }
      case 1: {  // try_pop_back — never asserts (returns false when empty)
        str.try_pop_back();
        break;
      }
      case 2: {  // assign from a bounded, NUL-terminated slice
        char buf[kCapacity + 1];
        in.c_string(buf, sizeof(buf));
        const bool ok = str.assign(buf);
        if (ok) {
          // On success the contents must equal the source exactly.
          if (std::strcmp(str.c_str(), buf) != 0) {
            __builtin_trap();
          }
        }
        break;
      }
      case 3: {  // append from a bounded, NUL-terminated slice
        char buf[kCapacity + 1];
        in.c_string(buf, sizeof(buf));
        str.append(buf);  // reports overflow via bool; contents unchanged on false
        break;
      }
      case 4: {  // append from a span (may contain embedded NUL, no termination needed)
        std::size_t len = 0;
        const std::uint8_t* p = in.slice(&len);
        str.append(metl::span<const char>(reinterpret_cast<const char*>(p), len));
        break;
      }
      case 5: {  // bounded index read (contract: index < size())
        if (!str.empty()) {
          const std::size_t idx = in.byte() % str.size();
          volatile char c = str[idx];
          (void)c;
          volatile char f = str.front();
          volatile char b = str.back();
          (void)f;
          (void)b;
        }
        break;
      }
      case 6: {  // read the whole string via its span view
        const metl::span<const char> view = str.as_span();
        std::size_t acc = 0;
        for (std::size_t i = 0; i < view.size(); ++i) {
          acc += static_cast<std::size_t>(view[i]);
        }
        (void)acc;
        break;
      }
      default: {  // clear
        str.clear();
        break;
      }
    }
    check_null_terminated(str);
    if (str.size() > str.capacity()) {
      __builtin_trap();
    }
  }

  return 0;
}
