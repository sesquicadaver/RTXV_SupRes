#pragma once

#include "types.hpp"

#include <string>
#include <utility>
#include <vector>

namespace rtx {

/// Накладає лише перелічені поля. Решта збереженого входу чи виходу лишається.
bool overlay_endpoint_flags(Endpoint& endpoint, const std::vector<std::pair<std::string, std::string>>& flags,
                            std::string& error);

int cli_main(int argc, char** argv);

}  // namespace rtx
