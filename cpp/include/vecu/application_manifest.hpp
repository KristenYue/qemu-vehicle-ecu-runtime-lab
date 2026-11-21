#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vecu {

enum class RestartPolicy { Never, OnFailure, Always };

struct ApplicationManifest {
    std::string name;
    std::vector<std::string> command;
    std::vector<std::string> dependencies;
    RestartPolicy restart_policy{RestartPolicy::Never};
    std::uint32_t max_restarts{0};
    std::uint32_t restart_backoff_ms{100};
    std::uint32_t start_timeout_ms{2000};
};

std::vector<ApplicationManifest> parse_manifest(const std::filesystem::path& path);
std::string to_string(RestartPolicy policy);

}  // namespace vecu

