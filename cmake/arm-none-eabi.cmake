# arm-none-eabi.cmake
#
# CMake toolchain file for cross-compiling METL with the GNU Arm Embedded
# Toolchain (arm-none-eabi-gcc / g++).
#
# Usage:
#   cmake -B build-arm -S . \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
#     -DMETL_ARM_CPU=cortex-m4 \
#     -DMETL_BUILD_TESTS=OFF \
#     -DMETL_BUILD_EMBEDDED_SMOKE=ON \
#     -DMETL_INSTALL=OFF
#
# Supported METL_ARM_CPU values: cortex-m0, cortex-m0plus, cortex-m3,
#                                cortex-m4, cortex-m7.

include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# Target system identification
# ---------------------------------------------------------------------------
set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Library compile check only: the bare-metal target has no host runtime
# linker, so don't try to link a test executable during compiler probing.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ---------------------------------------------------------------------------
# CPU selection
# ---------------------------------------------------------------------------
set(METL_ARM_CPU "cortex-m4" CACHE STRING
    "Target Cortex-M CPU (cortex-m0|cortex-m0plus|cortex-m3|cortex-m4|cortex-m7)")
set_property(CACHE METL_ARM_CPU PROPERTY STRINGS
    cortex-m0 cortex-m0plus cortex-m3 cortex-m4 cortex-m7)

# Per-CPU flag selection. The float-ABI and FPU follow the standard
# defaults for each Cortex-M part. Hardware-float ABIs are used when an
# FPU is present (M4F / M7F); soft-float otherwise.
if(METL_ARM_CPU STREQUAL "cortex-m0")
  set(_metl_arm_cpu_flags    "-mcpu=cortex-m0 -mthumb")
  set(_metl_arm_float_flags  "-mfloat-abi=soft")
elseif(METL_ARM_CPU STREQUAL "cortex-m0plus")
  set(_metl_arm_cpu_flags    "-mcpu=cortex-m0plus -mthumb")
  set(_metl_arm_float_flags  "-mfloat-abi=soft")
elseif(METL_ARM_CPU STREQUAL "cortex-m3")
  set(_metl_arm_cpu_flags    "-mcpu=cortex-m3 -mthumb")
  set(_metl_arm_float_flags  "-mfloat-abi=soft")
elseif(METL_ARM_CPU STREQUAL "cortex-m4")
  set(_metl_arm_cpu_flags    "-mcpu=cortex-m4 -mthumb")
  set(_metl_arm_float_flags  "-mfloat-abi=hard -mfpu=fpv4-sp-d16")
elseif(METL_ARM_CPU STREQUAL "cortex-m7")
  set(_metl_arm_cpu_flags    "-mcpu=cortex-m7 -mthumb")
  set(_metl_arm_float_flags  "-mfloat-abi=hard -mfpu=fpv5-d16")
else()
  message(FATAL_ERROR
    "Unsupported METL_ARM_CPU='${METL_ARM_CPU}'. "
    "Expected one of: cortex-m0, cortex-m0plus, cortex-m3, cortex-m4, cortex-m7")
endif()

# ---------------------------------------------------------------------------
# Compilers and binutils
# ---------------------------------------------------------------------------
set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_AR           arm-none-eabi-ar)
set(CMAKE_RANLIB       arm-none-eabi-ranlib)
set(CMAKE_OBJCOPY      arm-none-eabi-objcopy)
set(CMAKE_SIZE         arm-none-eabi-size)

# ---------------------------------------------------------------------------
# Compile flags
# ---------------------------------------------------------------------------
# Freestanding embedded code: no hosted libc assumptions, no exceptions,
# no RTTI, no unwind tables, gc-sections for dead-code elimination.
set(_metl_arm_common
  "${_metl_arm_cpu_flags} ${_metl_arm_float_flags} \
-ffreestanding \
-fno-exceptions -fno-rtti \
-fno-unwind-tables -fno-asynchronous-unwind-tables \
-ffunction-sections -fdata-sections")

set(CMAKE_C_FLAGS_INIT             "${_metl_arm_common}")
set(CMAKE_CXX_FLAGS_INIT           "${_metl_arm_common}")
set(CMAKE_ASM_FLAGS_INIT           "${_metl_arm_cpu_flags} ${_metl_arm_float_flags}")

# Default build-type flags. Embedded firmware almost always wants size
# optimization with debug symbols retained for in-circuit debugging.
set(CMAKE_C_FLAGS_DEBUG_INIT          "-Os -g")
set(CMAKE_CXX_FLAGS_DEBUG_INIT        "-Os -g")
set(CMAKE_C_FLAGS_RELEASE_INIT        "-Os")
set(CMAKE_CXX_FLAGS_RELEASE_INIT      "-Os")
set(CMAKE_C_FLAGS_MINSIZEREL_INIT     "-Os")
set(CMAKE_CXX_FLAGS_MINSIZEREL_INIT   "-Os")
set(CMAKE_C_FLAGS_RELWITHDEBINFO_INIT "-Os -g")
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
