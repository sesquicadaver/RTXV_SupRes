#pragma once

#include "types.hpp"

#include <string>

namespace rtx {

const GpuInfo* find_gpu(const std::vector<GpuInfo>& gpus, int index);
const GpuInfo* find_gpu_uuid(const std::vector<GpuInfo>& gpus, const std::string& uuid);

// Лише перелік пристроїв. Створення ефекту SDK сюди не входить.
std::string gpu_list_blocker(const Settings& settings, const DependencyReport& report);

// Якщо задано UUID, числовий індекс сеансу береться з поточного переліку.
std::string bind_gpu_selection(Settings& settings, const DependencyReport& report);

std::string doctor_json(const DependencyReport& report, const std::string& blockers, std::size_t mode_count);
std::string status_event(const std::string& state, const std::string& detail);
std::string metrics_event(const Metrics& metrics);

}  // namespace rtx
