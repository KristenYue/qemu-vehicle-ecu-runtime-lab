#include "vecu/application_manifest.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace vecu {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(delimiter, start);
        auto item = trim(value.substr(start, end == std::string::npos ? end : end - start));
        if (!item.empty()) {
            result.push_back(std::move(item));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return result;
}

std::uint32_t parse_u32(const std::string& value, const std::string& field, std::size_t line) {
    std::uint32_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto [ptr, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || ptr != end) {
        throw std::runtime_error("line " + std::to_string(line) + ": invalid " + field);
    }
    return parsed;
}

RestartPolicy parse_policy(const std::string& value, std::size_t line) {
    if (value == "never") return RestartPolicy::Never;
    if (value == "on-failure") return RestartPolicy::OnFailure;
    if (value == "always") return RestartPolicy::Always;
    throw std::runtime_error("line " + std::to_string(line) + ": invalid restart policy");
}

}  // namespace

std::vector<ApplicationManifest> parse_manifest(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open manifest: " + path.string());
    }

    std::vector<ApplicationManifest> applications;
    std::unordered_set<std::string> names;
    ApplicationManifest* current = nullptr;
    std::string raw;
    std::size_t line_number = 0;

    while (std::getline(input, raw)) {
        ++line_number;
        auto line = trim(raw);
        if (line.empty() || line.front() == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            auto name = trim(line.substr(1, line.size() - 2));
            if (name.empty() || !names.insert(name).second) {
                throw std::runtime_error("line " + std::to_string(line_number) + ": invalid or duplicate application");
            }
            applications.push_back(ApplicationManifest{});
            current = &applications.back();
            current->name = std::move(name);
            continue;
        }

        if (current == nullptr) {
            throw std::runtime_error("line " + std::to_string(line_number) + ": field before application section");
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            throw std::runtime_error("line " + std::to_string(line_number) + ": expected key=value");
        }
        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1));
        if (key == "command") {
            current->command = split(value, ' ');
        } else if (key == "depends") {
            current->dependencies = split(value, ',');
        } else if (key == "restart") {
            current->restart_policy = parse_policy(value, line_number);
        } else if (key == "max_restarts") {
            current->max_restarts = parse_u32(value, key, line_number);
        } else if (key == "restart_backoff_ms") {
            current->restart_backoff_ms = parse_u32(value, key, line_number);
        } else if (key == "start_timeout_ms") {
            current->start_timeout_ms = parse_u32(value, key, line_number);
        } else if (key == "heartbeat_timeout_ms") {
            current->heartbeat_timeout_ms = parse_u32(value, key, line_number);
        } else {
            throw std::runtime_error("line " + std::to_string(line_number) + ": unknown field " + key);
        }
    }

    if (applications.empty()) {
        throw std::runtime_error("manifest contains no applications");
    }
    for (const auto& app : applications) {
        if (app.command.empty()) {
            throw std::runtime_error("application " + app.name + " has no command");
        }
        if (app.start_timeout_ms == 0 || app.heartbeat_timeout_ms == 0) {
            throw std::runtime_error("application " + app.name + " has a zero health timeout");
        }
        for (const auto& dependency : app.dependencies) {
            if (!names.contains(dependency)) {
                throw std::runtime_error("application " + app.name + " depends on unknown application " + dependency);
            }
            if (dependency == app.name) {
                throw std::runtime_error("application " + app.name + " depends on itself");
            }
        }
    }
    return applications;
}

std::string to_string(RestartPolicy policy) {
    switch (policy) {
        case RestartPolicy::Never: return "never";
        case RestartPolicy::OnFailure: return "on-failure";
        case RestartPolicy::Always: return "always";
    }
    throw std::logic_error("unknown restart policy");
}

}  // namespace vecu

