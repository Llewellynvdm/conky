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
 * Copyright (c) 2008 Markus Meissner
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
#ifndef NVIDIA_NVML_CONKY_H
#define NVIDIA_NVML_CONKY_H

#include "../../content/specials.h"

#include <cstdint>
#include <memory>
#include <string>

#include "nvml.h"

void shutdown_nvml();

class Device {
 public:
  static std::unique_ptr<Device> create(unsigned int gpu_index);
  Device(nvmlDevice_t);

  ~Device();

  struct MemInfo {
    nvmlReturn_t status;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint64_t used_bytes;
  };
  MemInfo get_mem_info() const;

  int get_gpu_temp() const;

  unsigned int get_gpu_freq() const;
  unsigned int get_mem_freq() const;
  unsigned int get_sm_freq() const;
  unsigned int get_video_freq() const;
  unsigned int get_video_dec_util() const;
  unsigned int get_video_enc_util() const;

  unsigned int get_pcie_throughput_tx() const;
  unsigned int get_pcie_throughput_rx() const;

  unsigned int get_gpu_util() const;

  nvmlPstates_t get_pstate() const;

  unsigned int get_fan_speed() const;
  unsigned int get_fan_level() const;

  unsigned int get_power_one_sec_avg() const;  // In milliwatts
  unsigned int get_power_instant() const;      // In milliwatts

  const char* get_architecture_name() const;

  template <typename T>
  struct DeviceField {
    bool was_queried;
    bool is_supported;
    T value;

    DeviceField() : was_queried(false), is_supported(false), value() {}

    void set_queried() { this->was_queried = true; }

    void set_unsupported() {
      this->was_queried = true;
      this->is_supported = false;
    }

    void set(T value) {
      this->was_queried = true;
      this->is_supported = true;
      this->value = value;
    }
  };

  const DeviceField<std::string>& get_model_name() const {
    return this->model_name;
  }

  const DeviceField<nvmlPstates_t>& get_pstate_min() const {
    return this->pstate_min;
  }
  const DeviceField<nvmlPstates_t>& get_pstate_max() const {
    return this->pstate_max;
  }

  const DeviceField<uint64_t>& get_mem_total() const { return this->mem_total; }

  const DeviceField<int>& get_gpu_slowdown_temp() const {
    return this->gpu_slowdown_temp;
  }

  const DeviceField<unsigned int>& get_gpu_freq_min_clock_mhz() const {
    return this->gpu_freq_min_clock_mhz;
  }
  const DeviceField<unsigned int>& get_gpu_freq_max_clock_mhz() const {
    return this->gpu_freq_max_clock_mhz;
  }

  const DeviceField<unsigned int>& get_mem_freq_min_clock_mhz() const {
    return this->mem_freq_min_clock_mhz;
  }
  const DeviceField<unsigned int>& get_mem_freq_max_clock_mhz() const {
    return this->mem_freq_max_clock_mhz;
  }

  const DeviceField<unsigned int>& get_power_limit() const {
    return this->power_limit;
  }

 private:
  nvmlDevice_t device;

  DeviceField<std::string> model_name;

  DeviceField<nvmlPstates_t> pstate_min;
  DeviceField<nvmlPstates_t> pstate_max;

  // These are in Celsius
  DeviceField<int> gpu_shutdown_temp;
  DeviceField<int> gpu_slowdown_temp;
  DeviceField<int> mem_throttle_temp;
  DeviceField<int> gpu_freq_throttle_temp;

  DeviceField<unsigned int> gpu_freq_min_clock_mhz;
  DeviceField<unsigned int> gpu_freq_max_clock_mhz;

  DeviceField<unsigned int> mem_freq_min_clock_mhz;
  DeviceField<unsigned int> mem_freq_max_clock_mhz;

  DeviceField<unsigned int> sm_freq_min_clock_mhz;
  DeviceField<unsigned int> sm_freq_max_clock_mhz;

  DeviceField<unsigned int> video_freq_min_clock_mhz;
  DeviceField<unsigned int> video_freq_max_clock_mhz;

  DeviceField<unsigned int> power_limit;  // In milliwatts

  DeviceField<uint64_t> mem_total;  // In bytes

  DeviceField<unsigned int> num_fans;

  void query_model_name();
  void query_pstate_min_max();

  void query_max_temps_fail_with_info(nvmlReturn_t, const char*);
  void query_max_temps();
  void query_temp_thresholds();

  void get_min_max_clocks(nvmlClockType_t, DeviceField<unsigned int>*,
                          const char*, DeviceField<unsigned int>*, const char*);
  void query_freq_min_max();
  void query_mem_freq_min_max();
  void query_sm_freq_min_max();
  void query_video_freq_min_max();

  void query_power_limit();
  void query_mem_total();
  void query_num_fans();

  unsigned int get_clock_freq(nvmlClockType_t, bool*) const;
};

enum class NVMLQueryType : uint8_t {
  Text,
  Bar,
  Gauge,
  Graph,
};

bool create_nvml_query(text_object*, const char*, NVMLQueryType);

void print_nvml_value(text_object*, char*, unsigned int);
double get_nvml_percent_value(text_object*);
void free_nvml_query(text_object*);

#endif
