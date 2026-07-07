# ---------------------------------------------------------------------------
# libFuzzer harnesses (clang-only, opt-in via -DMETL_BUILD_FUZZERS=ON)
#
# Each harness is a LLVMFuzzerTestOneInput(const uint8_t*, size_t) that drives a
# metl target with the fuzz bytes, performing only CONTRACT-VALID operations so
# any ASan/UBSan finding is a genuine defect (see fuzz/fuzz_helpers.hpp). Built
# with -fsanitize=fuzzer,address,undefined. This directory is only added when
# METL_BUILD_FUZZERS is ON *and* the compiler is Clang, so a non-clang or
# option-off configure is completely unaffected.
# ---------------------------------------------------------------------------

set(_metl_fuzz_targets
  fuzz_fixed_string
  fuzz_flat_map
  fuzz_static_unordered_map
  fuzz_allocators
  fuzz_crc
)

# libFuzzer + sanitizers. Fuzzer instrumentation must be applied to both compile
# and link. ASan+UBSan give the actual bug-finding signal.
set(_metl_fuzz_flags -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer -g -O1)

foreach(_t IN LISTS _metl_fuzz_targets)
  add_executable(metl_${_t} ${_t}.cpp)
  target_link_libraries(metl_${_t} PRIVATE metl::metl)
  target_include_directories(metl_${_t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_features(metl_${_t} PRIVATE cxx_std_17)
  target_compile_options(metl_${_t} PRIVATE ${_metl_fuzz_flags})
  target_link_options(metl_${_t} PRIVATE ${_metl_fuzz_flags})
  set_target_properties(metl_${_t} PROPERTIES METL_KIND fuzzer)
endforeach()
