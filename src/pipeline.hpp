#pragma once

#include "endpoint.hpp"

namespace rtx {

int run_proxy(const Settings& settings, const std::vector<Mode>& modes, const OptionProbe& probe,
              Metrics* summary = nullptr);

}  // namespace rtx
