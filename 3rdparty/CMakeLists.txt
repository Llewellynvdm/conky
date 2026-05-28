# Vc version 1.4.4
add_subdirectory(Vc)

add_subdirectory(toluapp)
add_subdirectory(spdlog)

if(BUILD_NVIDIA_NVML)
  add_subdirectory(nvml)
endif(BUILD_NVIDIA_NVML)
