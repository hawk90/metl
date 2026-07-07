# riscv-none-elf.cmake
#
# CMake toolchain file for cross-compiling METL with a bare-metal (newlib)
# RISC-V GNU toolchain (riscv64-unknown-elf-gcc / g++).
#
# Usage:
#   cmake -B build-riscv -S . \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/riscv-none-elf.cmake \
#     -DMETL_RISCV_ARCH=rv32imac \
#     -DMETL_BUILD_TESTS=OFF \
#     -DMETL_BUILD_EMBEDDED_SMOKE=ON \
#     -DMETL_INSTALL=OFF
#
# Supported METL_RISCV_ARCH values: rv32imac (RV32, ilp32 ABI),
#                                   rv64     (RV64, lp64 ABI).
#
# This mirrors cmake/arm-none-eabi.cmake: it configures a freestanding,
# compile-only static-library build (no host runtime linker is involved).

include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# Target system identification
# ---------------------------------------------------------------------------
set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

# Library compile check only: the bare-metal target has no host runtime
# linker, so don't try to link a test executable during compiler probing.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ---------------------------------------------------------------------------
# ISA / ABI selection
# ---------------------------------------------------------------------------
set(METL_RISCV_ARCH "rv32imac" CACHE STRING
    "Target RISC-V ISA/ABI (rv32imac|rv64)")
set_property(CACHE METL_RISCV_ARCH PROPERTY STRINGS rv32imac rv64)

# Soft-float ABIs (no F/D extensions in the base -march) so the compile check
# does not depend on an FPU multilib being installed. Link is never performed.
if(METL_RISCV_ARCH STREQUAL "rv32imac")
  set(_metl_riscv_arch_flags "-march=rv32imac -mabi=ilp32")
elseif(METL_RISCV_ARCH STREQUAL "rv64")
  set(_metl_riscv_arch_flags "-march=rv64imac -mabi=lp64")
else()
  message(FATAL_ERROR
    "Unsupported METL_RISCV_ARCH='${METL_RISCV_ARCH}'. "
    "Expected one of: rv32imac, rv64")
endif()

# ---------------------------------------------------------------------------
# Compilers and binutils
# ---------------------------------------------------------------------------
set(CMAKE_C_COMPILER   riscv64-unknown-elf-gcc)
set(CMAKE_CXX_COMPILER riscv64-unknown-elf-g++)
set(CMAKE_ASM_COMPILER riscv64-unknown-elf-gcc)
set(CMAKE_AR           riscv64-unknown-elf-ar)
set(CMAKE_RANLIB       riscv64-unknown-elf-ranlib)
set(CMAKE_OBJCOPY      riscv64-unknown-elf-objcopy)
set(CMAKE_SIZE         riscv64-unknown-elf-size)

# ---------------------------------------------------------------------------
# Compile flags
# ---------------------------------------------------------------------------
# Freestanding embedded code: no hosted libc assumptions, no exceptions,
# no RTTI, no unwind tables, gc-sections for dead-code elimination.
set(_metl_riscv_common
  "${_metl_riscv_arch_flags} \
-ffreestanding \
-fno-exceptions -fno-rtti \
-fno-unwind-tables -fno-asynchronous-unwind-tables \
-ffunction-sections -fdata-sections")

set(CMAKE_C_FLAGS_INIT   "${_metl_riscv_common}")
set(CMAKE_CXX_FLAGS_INIT "${_metl_riscv_common}")
set(CMAKE_ASM_FLAGS_INIT "${_metl_riscv_arch_flags}")

# Default build-type flags. Embedded firmware almost always wants size
# optimization with debug symbols retained for in-circuit debugging.
set(CMAKE_C_FLAGS_DEBUG_INIT            "-Os -g")
set(CMAKE_CXX_FLAGS_DEBUG_INIT          "-Os -g")
set(CMAKE_C_FLAGS_RELEASE_INIT          "-Os")
set(CMAKE_CXX_FLAGS_RELEASE_INIT        "-Os")
set(CMAKE_C_FLAGS_MINSIZEREL_INIT       "-Os")
set(CMAKE_CXX_FLAGS_MINSIZEREL_INIT     "-Os")
set(CMAKE_C_FLAGS_RELWITHDEBINFO_INIT   "-Os -g")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT "-Os -g")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# find_* search policy
# ---------------------------------------------------------------------------
# Programs (host tools like git, python) are found on the host.
# Libraries and headers are only searched inside the cross-toolchain root.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
