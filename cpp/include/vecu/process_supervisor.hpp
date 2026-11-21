#pragma once

#include "vecu/application_manifest.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace vecu {

enum class ProcessState { Pending, Backoff, Running, Stopped, Failed };

struct ManagedProcess {
    ApplicationManifest manifest;
    ProcessState state{ProcessState::Pending};
    int pid{-1};
    std::uint32_t restart_count{0};
    std::uint64_t restart_at_ms{0};
};

class ProcessSupervisor {
public:
    explicit ProcessSupervisor(std::vector<ApplicationManifest> applications);
    int run();

private:
    void start_ready_processes(std::uint64_t now_ms);
    bool dependencies_running(const ManagedProcess& process) const;
    bool dependency_failed(const ManagedProcess& process) const;
    void spawn(ManagedProcess& process);
    void reap_children(std::uint64_t now_ms);
    void handle_exit(ManagedProcess& process, int wait_status, std::uint64_t now_ms);
    bool all_terminal() const;

    std::vector<std::string> startup_order_;
    std::unordered_map<std::string, ManagedProcess> processes_;
    int epoll_fd_{-1};
    int signal_fd_{-1};
    int timer_fd_{-1};
};

}  // namespace vecu

