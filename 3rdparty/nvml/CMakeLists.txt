# version is 13.3.29
# from https://developer.download.nvidia.com/compute/cuda/redist/cuda_nvml_dev/linux-x86_64/cuda_nvml_dev-linux-x86_64-13.3.29-archive.tar.xz

set(NVML_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/3rdparty/nvml" CACHE PATH
  "Directory containing NVML header")
set(NVML_LIB "${CMAKE_SOURCE_DIR}/3rdparty/nvml/libnvidia-ml.so" CACHE FILEPATH
  "Path to NVML libary")
