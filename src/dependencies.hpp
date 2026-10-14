#pragma once

#include "types.hpp"

#include <string>

namespace rtx {

DependencyReport inspect_dependencies();
std::string dependency_blockers(const Settings& settings, const DependencyReport& report);

}  // namespace rtx
