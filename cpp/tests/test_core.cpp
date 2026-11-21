#include "vecu/application_manifest.hpp"
#include "vecu/dependency_graph.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path write_manifest(const std::string& name, const std::string& content) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream output(path, std::ios::trunc);
    output << content;
    return path;
}

void test_valid_manifest_and_order() {
    const auto path = write_manifest("vecu-valid.manifest", R"(
[base]
command=/bin/base --ready
depends=
restart=on-failure
max_restarts=3
restart_backoff_ms=50
start_timeout_ms=500

[client]
command=/bin/client
depends=base
restart=never
max_restarts=0
restart_backoff_ms=100
start_timeout_ms=1000
)");
    const auto apps = vecu::parse_manifest(path);
    require(apps.size() == 2, "expected two applications");
    require(apps[0].command.size() == 2, "command arguments were not parsed");
    require(apps[0].max_restarts == 3, "restart budget was not parsed");
    const auto order = vecu::startup_order(apps);
    require(order.size() == 2 && order[0] == "base" && order[1] == "client", "invalid startup order");
    std::filesystem::remove(path);
}

void test_unknown_dependency_rejected() {
    const auto path = write_manifest("vecu-unknown.manifest", R"(
[client]
command=/bin/client
depends=missing
)");
    bool rejected = false;
    try { (void)vecu::parse_manifest(path); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "unknown dependency must be rejected");
    std::filesystem::remove(path);
}

void test_cycle_rejected() {
    const auto path = write_manifest("vecu-cycle.manifest", R"(
[a]
command=/bin/a
depends=b
[b]
command=/bin/b
depends=a
)");
    const auto apps = vecu::parse_manifest(path);
    bool rejected = false;
    try { (void)vecu::startup_order(apps); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "dependency cycle must be rejected");
    std::filesystem::remove(path);
}

void test_unknown_field_rejected() {
    const auto path = write_manifest("vecu-field.manifest", R"(
[a]
command=/bin/a
shell=true
)");
    bool rejected = false;
    try { (void)vecu::parse_manifest(path); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "unknown fields must be rejected");
    std::filesystem::remove(path);
}

}  // namespace

int main() {
    try {
        test_valid_manifest_and_order();
        test_unknown_dependency_rejected();
        test_cycle_rejected();
        test_unknown_field_rejected();
        std::cout << "vecu core tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "vecu core tests failed: " << error.what() << '\n';
        return 1;
    }
}

