/*
 *
 * Conky, a system monitor, based on torsmo
 *
 * Any original torsmo code is licensed under the BSD license
 *
 * All code written since the fork of torsmo is licensed under the GPL
 *
 * Please see COPYING for details
 *
 * Copyright (c) 2004, Hannu Saransaari and Lauri Hakkarainen
 * Copyright (c) 2005-2024 Brenden Matthews, Philip Kovacs, et. al.
 *	(see AUTHORS)
 * All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "nvidia_nvml.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "nvml.h"

#include "../../conky.h"
#include "../../logging.h"

void init_nvml();

struct nvml_init {
  bool is_init = false;
  std::string driver_version = "";
  std::string nvml_version = "";

  std::vector<std::unique_ptr<Device>> devices;

  Device* get_or_create_device(unsigned int gpu_index) {
    init_nvml();

    if (gpu_index >= this->devices.size()) { return nullptr; }

    auto device_ptr = this->devices[gpu_index].get();
    if (device_ptr) { return device_ptr; }

    auto device = Device::create(gpu_index);
    this->devices[gpu_index] = std::move(device);

    return this->devices[gpu_index].get();
  }
};

static nvml_init init{};

void init_nvml() {
  if (init.is_init) return;

  auto ret = nvmlInit_v2();
  if (ret != NVML_SUCCESS) {
    LOG_ERROR("Unable to initialize NVML: {}", nvmlErrorString(ret));
    return;
  }
  init.is_init = true;

  constexpr unsigned int dvsize = NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE;
  init.driver_version.resize(dvsize, 0);

  ret = nvmlSystemGetDriverVersion(init.driver_version.data(), dvsize);
  if (ret != NVML_SUCCESS) {
    LOG_WARNING("Unable to get NVIDIA driver version: {}",
                nvmlErrorString(ret));
    return;
  }
  LOG_DEBUG("NVIDIA driver version is {}", init.driver_version);

  constexpr unsigned int nvmlsize = NVML_SYSTEM_NVML_VERSION_BUFFER_SIZE;
  init.nvml_version.resize(nvmlsize, 0);

  ret = nvmlSystemGetNVMLVersion(init.nvml_version.data(), nvmlsize);
  if (ret != NVML_SUCCESS) {
    LOG_WARNING("Unable to get NVML version: {}", nvmlErrorString(ret));
    return;
  }
  LOG_DEBUG("NVML version is {}", init.nvml_version);

  unsigned int count = 0;
  ret = nvmlDeviceGetCount_v2(&count);
  if (ret != NVML_SUCCESS) {
    LOG_WARNING("Unable to get number of GPUs: {}", nvmlErrorString(ret));
    return;
  }
  LOG_DEBUG("Found {} NVIDIA GPUs", count);

  // init.devices
  for (unsigned int i = 0; i < count; i++) { init.devices.push_back(nullptr); }
}

std::unique_ptr<Device> Device::create(unsigned int gpu_index) {
  nvmlDevice_t d = nullptr;
  auto ret = nvmlDeviceGetHandleByIndex(gpu_index, &d);
  if (ret != NVML_SUCCESS) {
    LOG_DEBUG("Unable to get device for GPU {}", gpu_index);
    return nullptr;
  }

  auto device = std::make_unique<Device>(d);
  device->query_model_name();
  device->query_pstate_min_max();
  device->query_max_temps();
  device->query_freq_min_max();
  device->query_mem_freq_min_max();
  device->query_sm_freq_min_max();
  device->query_video_freq_min_max();
  device->query_power_limit();
  device->query_mem_total();

  return device;
}

// https://docs.nvidia.com/deploy/nvml-api/group__nvmlDeviceQueries.html
Device::Device(nvmlDevice_t d) : device(d) {}

Device::~Device() {}

void Device::query_model_name() {
  // Did we already call this?
  if (this->model_name.was_queried) { return; }

  constexpr unsigned int size = NVML_DEVICE_NAME_V2_BUFFER_SIZE;
  this->model_name.value.resize(size, 0);

  auto ret =
      nvmlDeviceGetName(this->device, this->model_name.value.data(), size);
  this->model_name.set_queried();
  this->model_name.is_supported = ret == NVML_SUCCESS;

  if (this->model_name.is_supported) {
    LOG_DEBUG("GPU device name is {}", this->model_name.value);
  } else {
    LOG_DEBUG("GPU device name not supported", this->model_name.value);
  }
}

void Device::query_pstate_min_max() {
  // Did we already call this?
  if (this->pstate_min.was_queried) { return; }

  constexpr unsigned int size = NVML_MAX_GPU_PERF_PSTATES;
  std::array<nvmlPstates_t, size> states;
  states.fill(NVML_PSTATE_UNKNOWN);

  // The size argument needs to be in bytes
  auto byte_size = size * sizeof(nvmlPstates_t);
  auto ret = nvmlDeviceGetSupportedPerformanceStates(this->device,
                                                     states.data(), byte_size);
  if (ret != NVML_SUCCESS) {
    this->pstate_min.set_unsupported();
    this->pstate_max.set_unsupported();
    return;
  }

  int max = 0;
  for (auto state : states) {
    if (state == NVML_PSTATE_UNKNOWN) { break; }
    if (state > max) { max = state; }
  }

  // The PStates are reversed with 0 being the max, and some value <= 15 for
  // min
  this->pstate_min.set(static_cast<nvmlPstates_t>(max));
  this->pstate_max.set(NVML_PSTATE_0);

  LOG_DEBUG("PStates are max: {}, min: {}",
            static_cast<int>(this->pstate_max.value),
            static_cast<int>(this->pstate_min.value));
}

void Device::query_max_temps_fail_with_info(nvmlReturn_t ret,
                                            const char* info) {
  LOG_DEBUG("{}: {}", info, nvmlErrorString(ret));

  this->gpu_shutdown_temp.set_unsupported();
  this->gpu_slowdown_temp.set_unsupported();
  this->mem_throttle_temp.set_unsupported();
  this->gpu_freq_throttle_temp.set_unsupported();
}

void Device::query_max_temps() {
  // Did we already call this?
  if (this->gpu_shutdown_temp.was_queried) { return; }

  /*
    TLIMITs are relative to the current temp + margin
    gpu_shutdown_temp = current + margin + (-shutdown_tlimit)
    gpu_slowdown_temp = current + margin + (-slowdown_tlimit)
    mem_throttle_temp = current + margin + (-mem_max_tlimit)
    gpu_freq_throttle_temp = current + margin + (-gpu_max_tlimit)

    This info was learned from a combination of the man page for nvidia-smi
    and this forum post from an Nvidia employee:
    https://forums.developer.nvidia.com/t/understanding-optimal-gpu-temperature-and-default-gpu-fan-curve-nvidia-rtx-6000-ada/361024

    Note that nvmlDeviceGetTemperatureThreshold wasn't used as the
    documentation says that it will soon be deprecated and/or removed.
   */

  // First get the current temp
  nvmlTemperature_t query_temp = {};
  query_temp.version = nvmlTemperature_v1;
  query_temp.sensorType = NVML_TEMPERATURE_GPU;
  auto ret = nvmlDeviceGetTemperatureV(this->device, &query_temp);
  if (ret != NVML_SUCCESS) {
    this->query_max_temps_fail_with_info(ret,
                                         "Unable to get current temperature");
    return;
  }
  int current = query_temp.temperature;

  // Next get the margin
  nvmlMarginTemperature_t query_margin = {};
  query_margin.version = nvmlMarginTemperature_v1;
  ret = nvmlDeviceGetMarginTemperature(this->device, &query_margin);
  if (ret != NVML_SUCCESS) {
    this->query_max_temps_fail_with_info(
        ret, "Unable to get current margin temperature");
    return;
  }
  int margin = query_margin.marginTemperature;

  // Now get all the TLIMITs
  nvmlFieldValue_t queries[4] = {};
  // shutdown_tlimit
  queries[0].fieldId = NVML_FI_DEV_TEMPERATURE_SHUTDOWN_TLIMIT;
  // slowdown_tlimit
  queries[1].fieldId = NVML_FI_DEV_TEMPERATURE_SLOWDOWN_TLIMIT;
  // mem_max_tlimit
  queries[2].fieldId = NVML_FI_DEV_TEMPERATURE_MEM_MAX_TLIMIT;
  // gpu_max_tlimit
  queries[3].fieldId = NVML_FI_DEV_TEMPERATURE_GPU_MAX_TLIMIT;

  ret = nvmlDeviceGetFieldValues(this->device, 4, queries);
  if (ret != NVML_SUCCESS) {
    this->query_max_temps_fail_with_info(ret, "Unable to get TLIMIT values");
    return;
  }

  // gpu_shutdown_temp
  if (queries[0].nvmlReturn == NVML_SUCCESS) {
    int shutdown_tlimit = queries[0].value.siVal;
    this->gpu_shutdown_temp.set(current + margin + (-shutdown_tlimit));

    LOG_TRACE("gpu_shutdown_temp: {}", this->gpu_shutdown_temp.value);
  } else {
    this->gpu_shutdown_temp.set_unsupported();
  }

  // gpu_slowdown_temp
  if (queries[1].nvmlReturn == NVML_SUCCESS) {
    int slowdown_tlimit = queries[1].value.siVal;
    this->gpu_slowdown_temp.set(current + margin + (-slowdown_tlimit));

    LOG_TRACE("gpu_slowdown_temp: {}", this->gpu_slowdown_temp.value);
  } else {
    this->gpu_slowdown_temp.set_unsupported();
  }

  // mem_throttle_temp
  if (queries[2].nvmlReturn == NVML_SUCCESS) {
    int mem_max_tlimit = queries[2].value.siVal;
    this->mem_throttle_temp.set(current + margin + (-mem_max_tlimit));

    LOG_TRACE("mem_throttle_temp: {}", this->mem_throttle_temp.value);
  } else {
    this->mem_throttle_temp.set_unsupported();
  }

  // gpu_freq_throttle_temp
  if (queries[3].nvmlReturn == NVML_SUCCESS) {
    int gpu_max_tlimit = queries[3].value.siVal;
    this->gpu_freq_throttle_temp.set(current + margin + (-gpu_max_tlimit));

    LOG_TRACE("gpu_freq_throttle_temp: {}", this->gpu_freq_throttle_temp.value);
  } else {
    this->gpu_freq_throttle_temp.set_unsupported();
  }
}

void Device::get_min_max_clocks(nvmlClockType_t clock,
                                DeviceField<unsigned int>* out_min,
                                const char* min_name,
                                DeviceField<unsigned int>* out_max,
                                const char* max_name) {
  unsigned int min = 0;
  unsigned int max = 0;

  // The min frequency is the min for pstate_min
  auto ret = nvmlDeviceGetMinMaxClockOfPState(
      this->device, clock, this->pstate_min.value, &min, &max);
  if (ret != NVML_SUCCESS) {
    LOG_DEBUG("{} is unsupported: {}", min_name, nvmlErrorString(ret));
    out_min->set_unsupported();
  } else {
    out_min->set(min);
    LOG_DEBUG("{} is {}", min_name, min);
  }

  // The max frequency is the max for pstate_max
  min = 0;
  max = 0;
  ret = nvmlDeviceGetMinMaxClockOfPState(this->device, clock,
                                         this->pstate_max.value, &min, &max);
  if (ret != NVML_SUCCESS) {
    LOG_DEBUG("{} is unsupported: {}", max_name, nvmlErrorString(ret));
    out_max->set_unsupported();
  } else {
    out_max->set(max);
    LOG_DEBUG("{} is {}", max_name, max);
  }
}

void Device::query_freq_min_max() {
  // Did we already call this?
  if (this->gpu_freq_min_clock_mhz.was_queried) { return; }

  this->get_min_max_clocks(NVML_CLOCK_GRAPHICS, &this->gpu_freq_min_clock_mhz,
                           "gpu_freq_min_clock_mhz",
                           &this->gpu_freq_max_clock_mhz,
                           "gpu_freq_max_clock_mhz");
}

void Device::query_mem_freq_min_max() {
  // Did we already call this?
  if (this->mem_freq_min_clock_mhz.was_queried) { return; }

  this->get_min_max_clocks(
      NVML_CLOCK_MEM, &this->mem_freq_min_clock_mhz, "mem_freq_min_clock_mhz",
      &this->mem_freq_max_clock_mhz, "mem_freq_max_clock_mhz");
}

void Device::query_sm_freq_min_max() {
  // Did we already call this?
  if (this->sm_freq_min_clock_mhz.was_queried) { return; }

  this->get_min_max_clocks(
      NVML_CLOCK_SM, &this->sm_freq_min_clock_mhz, "sm_freq_min_clock_mhz",
      &this->sm_freq_max_clock_mhz, "sm_freq_max_clock_mhz");
}

void Device::query_video_freq_min_max() {
  // Did we already call this?
  if (this->video_freq_min_clock_mhz.was_queried) { return; }

  this->get_min_max_clocks(NVML_CLOCK_VIDEO, &this->video_freq_min_clock_mhz,
                           "video_freq_min_clock_mhz",
                           &this->video_freq_max_clock_mhz,
                           "video_freq_max_clock_mhz");
}

void Device::query_power_limit() {
  // Did we already call this?
  if (this->power_limit.was_queried) { return; }

  nvmlFieldValue_t query = {};
  query.fieldId = NVML_FI_DEV_POWER_CURRENT_LIMIT;
  auto ret = nvmlDeviceGetFieldValues(this->device, 1, &query);
  if (ret != NVML_SUCCESS) {
    LOG_DEBUG("power limit is unsupported");
    this->power_limit.set_unsupported();
    return;
  }

  this->power_limit.set(query.value.uiVal);
  LOG_TRACE("power limit is: {} milliwatts", this->power_limit.value);
}

void Device::query_mem_total() {
  // Did we already call this?
  if (this->mem_total.was_queried) { return; }

  auto info = this->get_mem_info();
  if (info.status != NVML_SUCCESS) {
    this->mem_total.set_unsupported();
  } else {
    this->mem_total.set(info.total_bytes);
    LOG_TRACE("mem total is {} bytes", info.total_bytes);
  }
}

Device::MemInfo Device::get_mem_info() const {
  nvmlMemory_v2_t query = {};
  query.version = nvmlMemory_v2;
  auto status = nvmlDeviceGetMemoryInfo_v2(this->device, &query);

  return {
      .status = status,
      .total_bytes = query.total,
      .free_bytes = query.free,
      .used_bytes = query.used,
  };
}

int Device::get_gpu_temp() const {
  nvmlTemperature_t query_temp = {};
  query_temp.version = nvmlTemperature_v1;
  query_temp.sensorType = NVML_TEMPERATURE_GPU;
  auto ret = nvmlDeviceGetTemperatureV(this->device, &query_temp);
  if (ret != NVML_SUCCESS) {
    static bool did_warn = false;
    if (!did_warn) {
      LOG_WARNING("Unable to get GPU temperature: {}", nvmlErrorString(ret));
      did_warn = true;
    }
    return 0;
  }
  return query_temp.temperature;
}

unsigned int Device::get_clock_freq(nvmlClockType_t clock,
                                    bool* did_warn) const {
  static std::array<const char*, NVML_CLOCK_COUNT> names = {
      "GPU",
      "SM",
      "Mem",
      "Video",
  };

  unsigned int freq = 0;
  auto ret =
      nvmlDeviceGetClock(this->device, clock, NVML_CLOCK_ID_CURRENT, &freq);
  if (ret != NVML_SUCCESS) {
    if (!*did_warn) {
      auto name = names[static_cast<size_t>(clock)];
      LOG_WARNING("Unable to get {} frequency: {}", name, nvmlErrorString(ret));
      *did_warn = true;
    }
  }
  return freq;
}

unsigned int Device::get_gpu_freq() const {
  static bool did_warn = false;
  return this->get_clock_freq(NVML_CLOCK_GRAPHICS, &did_warn);
}

unsigned int Device::get_mem_freq() const {
  static bool did_warn = false;
  return this->get_clock_freq(NVML_CLOCK_MEM, &did_warn);
}

unsigned int Device::get_sm_freq() const {
  static bool did_warn = false;
  return this->get_clock_freq(NVML_CLOCK_SM, &did_warn);
}

unsigned int Device::get_video_freq() const {
  static bool did_warn = false;
  return this->get_clock_freq(NVML_CLOCK_VIDEO, &did_warn);
}

unsigned int Device::get_gpu_util() const {
  nvmlUtilization_t util = {};
  auto ret = nvmlDeviceGetUtilizationRates(this->device, &util);
  if (ret != NVML_SUCCESS) {
    static bool did_warn = false;
    if (!did_warn) {
      LOG_WARNING("Unable to get GPU utilization: {}", nvmlErrorString(ret));
      did_warn = true;
    }
    return 0;
  }
  return util.gpu;
}

nvmlPstates_t Device::get_pstate() const {
  nvmlPstates_t pstate = NVML_PSTATE_UNKNOWN;
  auto ret = nvmlDeviceGetPerformanceState(this->device, &pstate);
  if (ret != NVML_SUCCESS) {
    static bool did_warn = false;
    if (!did_warn) {
      LOG_WARNING("Unable to get performance state: {}", nvmlErrorString(ret));
      did_warn = true;
    }
  }
  return pstate;
}

unsigned int Device::get_fan_speed() const {
  nvmlFanSpeedInfo_t info = {};
  info.version = nvmlFanSpeedInfo_v1;
  info.fan = 0;
  auto ret = nvmlDeviceGetFanSpeedRPM(this->device, &info);
  if (ret != NVML_SUCCESS) {
    static bool did_warn = false;
    if (!did_warn) {
      LOG_WARNING("Unable to get fan speed: {}", nvmlErrorString(ret));
      did_warn = true;
    }
  }
  return info.speed;
}

unsigned int Device::get_fan_level() const {
  // Nvidia's docs have this note about nvmlDeviceGetFanSpeed_v2:
  // https://docs.nvidia.com/deploy/nvml-api/group__nvmlDeviceQueries.html#group__nvmlDeviceQueries_1g888c927906705a639b1ac4bdf6b7bfef)
  // The fan speed is expressed as a percentage of the product's maximum noise
  // tolerance fan speed. This value may exceed 100% in certain cases.

  // Since when is a percentage a speed?
  unsigned int level = 0;
  auto ret = nvmlDeviceGetFanSpeed_v2(this->device, 0, &level);
  if (ret != NVML_SUCCESS) {
    static bool did_warn = false;
    if (!did_warn) {
      LOG_WARNING("Unable to get fan level: {}", nvmlErrorString(ret));
      did_warn = true;
    }
  }
  return level;
}

unsigned int Device::get_power_one_sec_avg() const {
  nvmlFieldValue_t query = {};
  query.fieldId = NVML_FI_DEV_POWER_AVERAGE;
  auto ret = nvmlDeviceGetFieldValues(this->device, 1, &query);
  if (ret != NVML_SUCCESS) {
    static bool did_warn = false;
    if (!did_warn) {
      LOG_WARNING("Unable to get power one second average: {}",
                  nvmlErrorString(ret));
      did_warn = true;
    }
    return 0;
  }

  return query.value.uiVal;
}

unsigned int Device::get_power_instant() const {
  nvmlFieldValue_t query = {};
  query.fieldId = NVML_FI_DEV_POWER_INSTANT;
  auto ret = nvmlDeviceGetFieldValues(this->device, 1, &query);
  if (ret != NVML_SUCCESS) {
    static bool did_warn = false;
    if (!did_warn) {
      LOG_WARNING("Unable to get power: {}", nvmlErrorString(ret));
      did_warn = true;
    }
    return 0;
  }

  return query.value.uiVal;
}

const char* Device::get_architecture_name() const {
  // This is indexed by the NVML_DEVICE_ARCH_* constants
  static const char* names[] = {
      "Unknown",  // Unknown
      "Unknown",  // Unknown

      "Kepler",     // NVML_DEVICE_ARCH_KEPLER
      "Maxwell",    // NVML_DEVICE_ARCH_MAXWELL
      "Pascal",     // NVML_DEVICE_ARCH_PASCAL
      "Volta",      // NVML_DEVICE_ARCH_VOLTA
      "Turing",     // NVML_DEVICE_ARCH_TURING
      "Ampere",     // NVML_DEVICE_ARCH_AMPERE
      "Ada",        // NVML_DEVICE_ARCH_ADA
      "Hopper",     // NVML_DEVICE_ARCH_HOPPER
      "Blackwell",  // NVML_DEVICE_ARCH_BLACKWELL

      "Unknown",  // Unknown
      "Unknown",  // Unknown

      "Rubin",  // NVML_DEVICE_ARCH_RUBIN
  };

  nvmlDeviceArchitecture_t arch = 0;
  auto ret = nvmlDeviceGetArchitecture(this->device, &arch);
  if (ret != NVML_SUCCESS) {
    static bool did_warn = false;
    if (!did_warn) {
      LOG_WARNING("Unable to get GPU architecture");
      did_warn = true;
    }
    return names[0];
  }

  if (arch == NVML_DEVICE_ARCH_UNKNOWN) { return names[0]; }

  return names[arch];
}

void shutdown_nvml() {
  if (init.is_init) {
    (void)nvmlShutdown();
    init.is_init = false;
    init.driver_version = "";

    init.devices.clear();

    LOG_TRACE("shutdown NVML");
  }
}

enum class QueryAttribute : uint8_t {
  DriverVersion,
  Architecture,
  ModelName,

  GPUTemp,
  GPUTempThreshold,

  GPUFreq,
  GPUFreqMin,
  GPUFreqMax,

  GPUUtil,

  MemUsed,
  MemFree,
  MemMax,
  MemUtil,

  MemFreq,
  MemFreqMin,
  MemFreqMax,

  PerfLevelCur,
  PerfLevelMin,
  PerfLevelMax,

  FanSpeed,
  FanLevel,

  PowerLimit,
  PowerInstant,
  PowerAvg,
};

struct ParsedQuery {
  QueryAttribute attr;
  unsigned int gpu_id;

  ParsedQuery(QueryAttribute q, unsigned int id = 0) : attr(q), gpu_id(id) {}

  struct ParseAttrResult {
    bool success;
    QueryAttribute attr;
  };

  using attribute_map_type =
      std::unordered_map<std::string_view, QueryAttribute>;

  static const attribute_map_type attr_names_nvidia;
  static const attribute_map_type attr_names_nvidiabar;
  static const attribute_map_type attr_names_nvidiagauge;
  static const attribute_map_type attr_names_nvidiagraph;

  static ParseAttrResult parse_attribute(const attribute_map_type& map,
                                         std::string_view arg) {
    auto search = map.find(arg);
    if (search == map.end()) {
      ParseAttrResult res{};
      res.success = false;
      return res;
    }

    ParseAttrResult res{};
    res.success = true;
    res.attr = search->second;
    return res;
  }

  static std::pair<unsigned int, std::string_view> parse_gpu_id(
      const std::string_view& arg) {
    auto last_pos = arg.find_last_of(' ');
    if (last_pos != std::string_view::npos) {
      unsigned int gpu_id = 0;
      auto gpu_arg = arg.substr(last_pos + 1);
      auto _res = std::from_chars(gpu_arg.data(),
                                  gpu_arg.data() + gpu_arg.length(), gpu_id);
      // Ignore if std::from_chars failed as gpu_id will be 0

      // Remove the gpu_id from the end of arg
      return {gpu_id, arg.substr(0, last_pos)};
    }

    return {0, arg};
  }
};

const ParsedQuery::attribute_map_type ParsedQuery::attr_names_nvidia = {
    {"driverversion", QueryAttribute::DriverVersion},
    {"modelname", QueryAttribute::ModelName},
    {"arch", QueryAttribute::Architecture},
    {"gputemp", QueryAttribute::GPUTemp},
    {"gputempthreshold", QueryAttribute::GPUTempThreshold},
    {"gpufreqcur", QueryAttribute::GPUFreq},
    {"gpufreqmin", QueryAttribute::GPUFreqMin},
    {"gpufreqmax", QueryAttribute::GPUFreqMax},
    {"gpuutil", QueryAttribute::GPUUtil},
    {"memused", QueryAttribute::MemUsed},
    {"memfree", QueryAttribute::MemFree},
    {"memmax", QueryAttribute::MemMax},
    {"memutil", QueryAttribute::MemUtil},
    {"memfreqcur", QueryAttribute::MemFreq},
    {"memfreqmin", QueryAttribute::MemFreqMin},
    {"memfreqmax", QueryAttribute::MemFreqMax},
    {"perflevelcur", QueryAttribute::PerfLevelCur},
    {"perflevelmin", QueryAttribute::PerfLevelMin},
    {"perflevelmax", QueryAttribute::PerfLevelMax},
    {"fanspeed", QueryAttribute::FanSpeed},
    {"fanlevel", QueryAttribute::FanLevel},
    {"powerlimit", QueryAttribute::PowerLimit},
    {"powerinstant", QueryAttribute::PowerInstant},
    {"poweravg", QueryAttribute::PowerAvg},

    // Aliases
    {"temp", QueryAttribute::GPUTemp},
    {"threshold", QueryAttribute::GPUTempThreshold},
    {"gpufreq", QueryAttribute::GPUFreq},
    {"memfreq", QueryAttribute::MemFreq},
    {"perflevel", QueryAttribute::PerfLevelCur},
    {"mem", QueryAttribute::MemUsed},
    {"memavail", QueryAttribute::MemFree},
    {"memtotal", QueryAttribute::MemMax},
    {"memperc", QueryAttribute::MemUtil},
};

const ParsedQuery::attribute_map_type ParsedQuery::attr_names_nvidiabar = {
    {"gputemp", QueryAttribute::GPUTemp},
    {"gpufreqcur", QueryAttribute::GPUFreq},
    {"gpuutil", QueryAttribute::GPUUtil},
    {"memused", QueryAttribute::MemUsed},
    {"memfree", QueryAttribute::MemFree},
    {"memutil", QueryAttribute::MemUtil},
    {"memfreqcur", QueryAttribute::MemFreq},
    {"fanlevel", QueryAttribute::FanLevel},
    {"powerinstant", QueryAttribute::PowerInstant},
    {"poweravg", QueryAttribute::PowerAvg},

    // Aliases
    {"temp", QueryAttribute::GPUTemp},
    {"gpufreq", QueryAttribute::GPUFreq},
    {"memfreq", QueryAttribute::MemFreq},
    {"mem", QueryAttribute::MemUsed},
    {"memavail", QueryAttribute::MemFree},
    {"memperc", QueryAttribute::MemUtil},
};

const ParsedQuery::attribute_map_type ParsedQuery::attr_names_nvidiagauge =
    ParsedQuery::attr_names_nvidiabar;

const ParsedQuery::attribute_map_type ParsedQuery::attr_names_nvidiagraph =
    ParsedQuery::attr_names_nvidiabar;

/*
  This currently replicates most of the arguments from set_nvidia_query, but
  also adds new things related to power.

  Some nice improvements that are possible:
    A bunch of things related to temperature limits could be added:
      gpu_shutdown_temp, gpu_slowdown_temp, mem_throttle_temp, and
      gpu_freq_throttle_temp could all be exposed. Right now only
      gpu_slowdown_temp (as gputempthreshold) is available.

      Additionally the margin temperature could be exposed.

    Switch from ${nvidia arg (GPU_ID)} to ${nvidia (GPU_ID) arg}. This would
    enable passing additional arguments for the particular arg.
    For example:
      ${nvidia gpufreq GHz} could output the frequency into GHz
      ${nvidia perlevelcur false} could hide the P in P0 etc.
*/
bool create_nvml_query(text_object* obj, const char* arg, NVMLQueryType type) {
  if (!arg) { return false; }

  // Get the gpu_id, starting from the back of arg
  auto arg_view = std::string_view(arg);
  auto [gpu_id, rest_view] = ParsedQuery::parse_gpu_id(arg_view);

  // Get the argument
  auto pos = rest_view.find_first_of(' ');
  std::string_view attr_arg;
  if (pos == std::string_view::npos) {
    // Only one argument
    attr_arg = rest_view;
  } else {
    attr_arg = rest_view.substr(0, pos);

    // Chop attr_arg off of rest_view
    rest_view = rest_view.substr(pos + 1);
  }

  QueryAttribute attr;
  switch (type) {
    case NVMLQueryType::Text: {
      auto [success, parsed_attr] = ParsedQuery::parse_attribute(
          ParsedQuery::attr_names_nvidia, attr_arg);
      if (!success) { return false; }

      attr = parsed_attr;
      break;
    }

    case NVMLQueryType::Bar: {
      auto [success, parsed_attr] = ParsedQuery::parse_attribute(
          ParsedQuery::attr_names_nvidiabar, attr_arg);
      if (!success) { return false; }

      // Make sure scan_bar gets a null terminated string
      std::string rest_str = std::string(rest_view);
      scan_bar(obj, rest_str.c_str(), 100.0);

      attr = parsed_attr;
      break;
    }

    case NVMLQueryType::Gauge: {
      auto [success, parsed_attr] = ParsedQuery::parse_attribute(
          ParsedQuery::attr_names_nvidiagauge, attr_arg);
      if (!success) { return false; }

      // Make sure scan_gauge gets a null terminated string
      std::string rest_str = std::string(rest_view);
      scan_gauge(obj, rest_str.c_str(), 100.0);

      attr = parsed_attr;
      break;
    }

    case NVMLQueryType::Graph: {
      auto [success, parsed_attr] = ParsedQuery::parse_attribute(
          ParsedQuery::attr_names_nvidiagraph, attr_arg);
      if (!success) { return false; }

      // Make sure scan_graph gets a null terminated string
      std::string rest_str = std::string(rest_view);

      auto data_key =
          graph_data_key{fmt::format("nvidia:{}:{}", gpu_id, attr_arg)};
      scan_graph(obj, rest_str.c_str(), 100.0, FALSE, data_key);

      attr = parsed_attr;
      break;
    }
  }

  // Save the query in obj
  ParsedQuery* query = new ParsedQuery(attr, gpu_id);
  obj->data.opaque = query;
  return true;
}

void print_nvml_value(text_object* obj, char* output, unsigned int max_output) {
  auto query = static_cast<ParsedQuery*>(obj->data.opaque);
  auto device = init.get_or_create_device(query->gpu_id);
  if (!device) {
    LOG_DEBUG("No NVIDIA GPU {} available", query->gpu_id);
    return;
  }

  switch (query->attr) {
    case QueryAttribute::DriverVersion:
      snprintf(output, max_output, "%s", init.driver_version.c_str());
      break;

    case QueryAttribute::Architecture:
      snprintf(output, max_output, "%s", device->get_architecture_name());
      break;

    case QueryAttribute::ModelName: {
      const auto& model_name = device->get_model_name();
      if (!model_name.is_supported) {
        LOG_DEBUG("Unable to get modelname");
        break;
      }

      snprintf(output, max_output, "%s", model_name.value.c_str());
      break;
    }

    case QueryAttribute::GPUTemp: {
      auto temp = device->get_gpu_temp();
      snprintf(output, max_output, "%i", temp);
      break;
    }

    case QueryAttribute::GPUTempThreshold: {
      auto threshold = device->get_gpu_slowdown_temp();
      if (!threshold.is_supported) {
        LOG_DEBUG("Unable to get gputempthreshold");
        break;
      }

      snprintf(output, max_output, "%i", threshold.value);
      break;
    }

    case QueryAttribute::GPUFreq: {
      auto freq = device->get_gpu_freq();
      snprintf(output, max_output, "%u", freq);
      break;
    }

    case QueryAttribute::GPUFreqMin: {
      auto min = device->get_gpu_freq_min_clock_mhz();
      if (!min.is_supported) {
        LOG_DEBUG("Unable to get gpufreqmin");
        break;
      }

      snprintf(output, max_output, "%u", min.value);
      break;
    }

    case QueryAttribute::GPUFreqMax: {
      auto max = device->get_gpu_freq_max_clock_mhz();
      if (!max.is_supported) {
        LOG_DEBUG("Unable to get gpufreqmax");
        break;
      }

      snprintf(output, max_output, "%u", max.value);
      break;
    }

    case QueryAttribute::GPUUtil: {
      auto util = device->get_gpu_util();
      snprintf(output, max_output, "%u", util);
      break;
    }

    case QueryAttribute::MemUsed: {
      auto info = device->get_mem_info();
      if (info.status != NVML_SUCCESS) {
        LOG_DEBUG("Unable to get memused");
        break;
      }

      human_readable(static_cast<long long>(info.used_bytes), output,
                     max_output);
      break;
    }
    case QueryAttribute::MemFree: {
      auto info = device->get_mem_info();
      if (info.status != NVML_SUCCESS) {
        LOG_DEBUG("Unable to get memfree");
        break;
      }

      human_readable(static_cast<long long>(info.free_bytes), output,
                     max_output);
      break;
    }
    case QueryAttribute::MemMax: {
      const auto& mem_total = device->get_mem_total();
      if (!mem_total.is_supported) {
        LOG_DEBUG("Unable to get memmax");
        break;
      }
      human_readable(static_cast<long long>(mem_total.value), output,
                     max_output);
      break;
    }
    case QueryAttribute::MemUtil: {
      auto info = device->get_mem_info();
      if (info.status != NVML_SUCCESS) {
        LOG_DEBUG("Unable to get memutil");
        break;
      }
      unsigned int percent = (static_cast<double>(info.used_bytes) * 100 /
                              static_cast<double>(info.total_bytes)) +
                             0.5;
      percent_print(output, max_output, percent);
      break;
    }
    case QueryAttribute::MemFreq: {
      auto freq = device->get_mem_freq();
      snprintf(output, max_output, "%u", freq);
      break;
    }
    case QueryAttribute::MemFreqMin: {
      auto freq = device->get_mem_freq_min_clock_mhz();
      if (!freq.is_supported) {
        LOG_DEBUG("Unable to get memfreqmin");
        break;
      }

      snprintf(output, max_output, "%u", freq.value);
      break;
    }
    case QueryAttribute::MemFreqMax: {
      auto freq = device->get_mem_freq_max_clock_mhz();
      if (!freq.is_supported) {
        LOG_DEBUG("Unable to get memfreqmax");
        break;
      }

      snprintf(output, max_output, "%u", freq.value);
      break;
    }
    case QueryAttribute::PerfLevelCur: {
      auto pstate = device->get_pstate();
      if (pstate == NVML_PSTATE_UNKNOWN) { break; }

      snprintf(output, max_output, "P%i", static_cast<int>(pstate));
      break;
    }
    case QueryAttribute::PerfLevelMin: {
      auto min = device->get_pstate_min();
      if (!min.is_supported || min.value == NVML_PSTATE_UNKNOWN) {
        LOG_DEBUG("Unable to get perflevelmin");
        break;
      }

      snprintf(output, max_output, "P%i", static_cast<int>(min.value));
      break;
    }
    case QueryAttribute::PerfLevelMax: {
      auto max = device->get_pstate_max();
      if (!max.is_supported || max.value == NVML_PSTATE_UNKNOWN) {
        LOG_DEBUG("Unable to get perflevelmax");
        break;
      }

      snprintf(output, max_output, "P%i", static_cast<int>(max.value));
      break;
    }
    case QueryAttribute::FanSpeed: {
      auto speed = device->get_fan_speed();
      snprintf(output, max_output, "%u", speed);
      break;
    }
    case QueryAttribute::FanLevel: {
      auto level = device->get_fan_level();
      snprintf(output, max_output, "%u", level);
      break;
    }
    case QueryAttribute::PowerLimit: {
      auto limit = device->get_power_limit();
      if (!limit.is_supported) {
        LOG_DEBUG("Unable to get powerlimit");
        break;
      }

      // Convert to watts
      float val = limit.value / 1000.0f;
      snprintf(output, max_output, "%.3f", val);
      break;
    }
    case QueryAttribute::PowerInstant: {
      auto power = device->get_power_instant();

      // Convert to watts
      float val = power / 1000.0f;
      snprintf(output, max_output, "%.3f", val);
      break;
    }
    case QueryAttribute::PowerAvg: {
      auto power = device->get_power_one_sec_avg();

      // Convert to watts
      float val = power / 1000.0f;
      snprintf(output, max_output, "%.3f", val);
      break;
    }
  }
}

static std::pair<bool, Device::MemInfo> nvidiagraph_check_mem_info(
    const Device* device, const char* name) {
  if (!device->get_mem_total().is_supported) {
    LOG_DEBUG("Unable to graph {}", name);
    return {false, {}};
  }

  auto info = device->get_mem_info();
  if (info.status != NVML_SUCCESS) {
    LOG_DEBUG("Unable to graph {}", name);
    return {false, {}};
  }

  return {true, info};
}

static inline double calc_percent(double value, double max) {
  return (value * 100.0 / max) + 0.5;
}

static inline double calc_percent(double value, double min, double max) {
  return ((value - min) * 100.0 / (max - min)) + 0.5;
}

double get_nvml_percent_value(text_object* obj) {
  auto query = static_cast<ParsedQuery*>(obj->data.opaque);
  auto device = init.get_or_create_device(query->gpu_id);
  if (!device) {
    LOG_DEBUG("No GPU {} available", query->gpu_id);
    return 0;
  }

  switch (query->attr) {
    case QueryAttribute::DriverVersion:
    case QueryAttribute::Architecture:
    case QueryAttribute::ModelName:
    case QueryAttribute::GPUTempThreshold:
    case QueryAttribute::GPUFreqMin:
    case QueryAttribute::GPUFreqMax:
    case QueryAttribute::MemMax:
    case QueryAttribute::MemFreqMin:
    case QueryAttribute::MemFreqMax:
    case QueryAttribute::PerfLevelCur:
    case QueryAttribute::PerfLevelMin:
    case QueryAttribute::PerfLevelMax:
    case QueryAttribute::FanSpeed:
    case QueryAttribute::PowerLimit:
      // These are rejected earlier in create_nvml_nvidiagraph_query
      return 0;

    case QueryAttribute::GPUTemp: {
      auto max_temp = device->get_gpu_slowdown_temp();
      if (!max_temp.is_supported) {
        LOG_DEBUG("Unable to graph gputemp");
        return 0;
      }

      return calc_percent(device->get_gpu_temp(), max_temp.value);
    }
    case QueryAttribute::GPUFreq: {
      auto min_freq = device->get_gpu_freq_min_clock_mhz();
      auto max_freq = device->get_gpu_freq_max_clock_mhz();
      if (!min_freq.is_supported || !max_freq.is_supported) {
        LOG_DEBUG("Unable to graph gpufreqcur");
        return 0;
      }

      return calc_percent(device->get_gpu_freq(), min_freq.value,
                          max_freq.value);
    }
    case QueryAttribute::GPUUtil: {
      return device->get_gpu_util();
    }
    case QueryAttribute::MemUsed: {
      auto [good, info] = nvidiagraph_check_mem_info(device, "memused");
      if (!good) { return 0; }

      return calc_percent(info.used_bytes, info.total_bytes);
    }
    case QueryAttribute::MemFree: {
      auto [good, info] = nvidiagraph_check_mem_info(device, "memfree");
      if (!good) { return 0; }

      return calc_percent(info.free_bytes, info.total_bytes);
    }
    case QueryAttribute::MemUtil: {
      auto [good, info] = nvidiagraph_check_mem_info(device, "memutil");
      if (!good) { return 0; }

      return calc_percent(info.used_bytes, info.total_bytes);
    }
    case QueryAttribute::MemFreq: {
      auto freq_min = device->get_mem_freq_min_clock_mhz();
      auto freq_max = device->get_mem_freq_max_clock_mhz();
      if (!freq_min.is_supported || !freq_max.is_supported) {
        LOG_DEBUG("Unable to graph memfreqcur");
        return 0;
      }

      return calc_percent(device->get_mem_freq(), freq_min.value,
                          freq_max.value);
    }

    case QueryAttribute::FanLevel: {
      return device->get_fan_level();
    }

    case QueryAttribute::PowerInstant: {
      auto limit = device->get_power_limit();
      if (!limit.is_supported) {
        LOG_DEBUG("Unable to get powerinstant");
        return 0;
      }

      return calc_percent(device->get_power_instant(), limit.value);
    }
    case QueryAttribute::PowerAvg:
      auto limit = device->get_power_limit();
      if (!limit.is_supported) {
        LOG_DEBUG("Unable to get poweravg");
        return 0;
      }

      return calc_percent(device->get_power_one_sec_avg(), limit.value);
      break;
  }
  return 0;
}

void free_nvml_query(text_object* obj) {
  auto query = static_cast<ParsedQuery*>(obj->data.opaque);
  delete query;
  obj->data.opaque = nullptr;
}
