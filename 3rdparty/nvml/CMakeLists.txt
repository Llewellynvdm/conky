# NVML stub libraries sourced from:
# https://developer.download.nvidia.com/compute/cuda/redist/cuda_nvml_dev/

# Select the prebuilt stub matching the target architecture.
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(NVML_ARCH_DIR "linux-x86_64")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
  # CUDA 13 dropped the embedded linux-aarch64 (Tegra) redist; aarch64 is
  # frozen at 12.9.79. SBSA is the current ARM64 build and the stub is a
  # dlopen wrapper, so it's link- and runtime-compatible on any aarch64 host.
  set(NVML_ARCH_DIR "linux-sbsa")
elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc64le")
  set(NVML_ARCH_DIR "linux-ppc64le")
else()
  message(FATAL_ERROR
    "NVML: unsupported target architecture '${CMAKE_SYSTEM_PROCESSOR}'")
endif()

set(NVML_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/${NVML_ARCH_DIR}")
set(NVML_INCLUDE_DIR "${NVML_ROOT}/include")
set(NVML_LIB "${NVML_ROOT}/lib/libnvidia-ml.a")

# Expose the bundled NVML version (first line of the version file).
file(STRINGS "${NVML_ROOT}/version" NVIDIA_NVML_VERSION LIMIT_COUNT 1)
set(NVIDIA_NVML_VERSION "${NVIDIA_NVML_VERSION}" PARENT_SCOPE)

# Encode the version as a single totally-ordered integer AABBCCC (major*100000 +
# minor*1000 + patch), so code can compare it across platform versions. NVML's
# own NVML_API_VERSION only carries the major component, which is too coarse.
if(NOT NVIDIA_NVML_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
  message(FATAL_ERROR
    "NVML: unexpected version string '${NVIDIA_NVML_VERSION}' in ${NVML_ROOT}/version")
endif()
math(EXPR NVIDIA_NVML_VERSION_NUMBER
  "${CMAKE_MATCH_1} * 100000 + ${CMAKE_MATCH_2} * 1000 + ${CMAKE_MATCH_3}")

add_library(nvml STATIC IMPORTED GLOBAL)
set_target_properties(nvml PROPERTIES
  IMPORTED_LOCATION "${NVML_LIB}"
  INTERFACE_INCLUDE_DIRECTORIES "${NVML_INCLUDE_DIR}"
  INTERFACE_COMPILE_DEFINITIONS "NVIDIA_NVML_VERSION=${NVIDIA_NVML_VERSION_NUMBER}")
