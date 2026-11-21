#pragma once

#include "vecu/application_manifest.hpp"

#include <string>
#include <vector>

namespace vecu {

std::vector<std::string> startup_order(const std::vector<ApplicationManifest>& applications);

}  // namespace vecu

