#include "vecu/application_manifest.hpp"
#include "vecu/process_supervisor.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: vecu_execution_manager <manifest>\n";
        return 64;
    }
    try {
        vecu::ProcessSupervisor supervisor(vecu::parse_manifest(argv[1]));
        return supervisor.run();
    } catch (const std::exception& error) {
        std::cerr << "execution manager failed: " << error.what() << '\n';
        return 70;
    }
}

