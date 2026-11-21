#include "vecu/process_supervisor.hpp"

#include "vecu/dependency_graph.hpp"
#include "vecu/heartbeat_protocol.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <spawn.h>
#include <stdexcept>
#include <string_view>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <unistd.h>

extern char** environ;

namespace vecu {
namespace {

std::uint64_t monotonic_ms() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::uint64_t backoff_ms(const ManagedProcess& process) {
    const auto shift = std::min<std::uint32_t>(process.restart_count, 10U);
    return static_cast<std::uint64_t>(process.manifest.restart_backoff_ms) << shift;
}

void add_epoll(int epoll_fd, int fd) {
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
        throw std::runtime_error("epoll_ctl failed: " + std::string(std::strerror(errno)));
    }
}

}  // namespace

ProcessSupervisor::ProcessSupervisor(std::vector<ApplicationManifest> applications)
    : startup_order_(startup_order(applications)) {
    for (auto& app : applications) {
        const auto name = app.name;
        processes_.emplace(name, ManagedProcess{std::move(app)});
    }
}

int ProcessSupervisor::run() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mask, nullptr) == -1) {
        throw std::runtime_error("sigprocmask failed");
    }

    signal_fd_ = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    heartbeat_fd_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (signal_fd_ == -1 || timer_fd_ == -1 || heartbeat_fd_ == -1 || epoll_fd_ == -1) {
        throw std::runtime_error("failed to create Linux event-loop descriptors");
    }

    heartbeat_path_ = "/tmp/vecu-em-" + std::to_string(getpid()) + ".sock";
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (heartbeat_path_.size() >= sizeof(address.sun_path)) {
        throw std::runtime_error("heartbeat socket path is too long");
    }
    std::strncpy(address.sun_path, heartbeat_path_.c_str(), sizeof(address.sun_path) - 1);
    (void)unlink(heartbeat_path_.c_str());
    if (bind(heartbeat_fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) {
        throw std::runtime_error("failed to bind heartbeat socket: " + std::string(std::strerror(errno)));
    }

    itimerspec timer{};
    timer.it_interval.tv_nsec = 100'000'000;
    timer.it_value = timer.it_interval;
    if (timerfd_settime(timer_fd_, 0, &timer, nullptr) == -1) {
        throw std::runtime_error("timerfd_settime failed");
    }
    add_epoll(epoll_fd_, signal_fd_);
    add_epoll(epoll_fd_, timer_fd_);
    add_epoll(epoll_fd_, heartbeat_fd_);

    start_ready_processes(monotonic_ms());
    while (!all_terminal()) {
        epoll_event events[4]{};
        const int count = epoll_wait(epoll_fd_, events, 4, -1);
        if (count == -1 && errno == EINTR) continue;
        if (count == -1) throw std::runtime_error("epoll_wait failed");

        for (int i = 0; i < count; ++i) {
            if (events[i].data.fd == signal_fd_) {
                signalfd_siginfo signal_info{};
                while (read(signal_fd_, &signal_info, sizeof(signal_info)) == sizeof(signal_info)) {}
                reap_children(monotonic_ms());
            } else if (events[i].data.fd == timer_fd_) {
                std::uint64_t expirations = 0;
                (void)read(timer_fd_, &expirations, sizeof(expirations));
                enforce_health_deadlines(monotonic_ms());
            } else if (events[i].data.fd == heartbeat_fd_) {
                receive_heartbeats(monotonic_ms());
            }
        }
        start_ready_processes(monotonic_ms());
    }
    (void)unlink(heartbeat_path_.c_str());
    close(heartbeat_fd_);
    close(timer_fd_);
    close(signal_fd_);
    close(epoll_fd_);
    return 0;
}

void ProcessSupervisor::start_ready_processes(std::uint64_t now_ms) {
    for (const auto& name : startup_order_) {
        auto& process = processes_.at(name);
        const bool eligible = process.state == ProcessState::Pending ||
                              (process.state == ProcessState::Backoff && now_ms >= process.restart_at_ms);
        if (!eligible) continue;
        if (dependency_failed(process)) {
            process.state = ProcessState::Failed;
            std::cerr << "ts_ms=" << now_ms << " event=dependency_failed app="
                      << process.manifest.name << '\n';
        } else if (dependencies_ready(process)) {
            spawn(process);
        }
    }
}

bool ProcessSupervisor::dependency_failed(const ManagedProcess& process) const {
    for (const auto& dependency : process.manifest.dependencies) {
        const auto state = processes_.at(dependency).state;
        if (state == ProcessState::Failed || state == ProcessState::Stopped) return true;
    }
    return false;
}

bool ProcessSupervisor::dependencies_ready(const ManagedProcess& process) const {
    for (const auto& dependency : process.manifest.dependencies) {
        if (processes_.at(dependency).state != ProcessState::Ready) return false;
    }
    return true;
}

void ProcessSupervisor::spawn(ManagedProcess& process) {
    std::vector<char*> arguments;
    arguments.reserve(process.manifest.command.size() + 1);
    for (auto& item : process.manifest.command) arguments.push_back(item.data());
    arguments.push_back(nullptr);

    std::vector<std::string> environment_storage;
    for (char** item = environ; *item != nullptr; ++item) {
        const std::string_view entry(*item);
        if (entry.starts_with("VECU_HEARTBEAT_SOCKET=") || entry.starts_with("VECU_APP_NAME=")) continue;
        environment_storage.emplace_back(*item);
    }
    environment_storage.push_back("VECU_HEARTBEAT_SOCKET=" + heartbeat_path_);
    environment_storage.push_back("VECU_APP_NAME=" + process.manifest.name);
    std::vector<char*> environment;
    environment.reserve(environment_storage.size() + 1);
    for (auto& item : environment_storage) environment.push_back(item.data());
    environment.push_back(nullptr);

    pid_t pid = -1;
    const int result = posix_spawnp(&pid, arguments.front(), nullptr, nullptr, arguments.data(), environment.data());
    if (result != 0) {
        process.state = ProcessState::Failed;
        std::cerr << "ts_ms=" << monotonic_ms() << " event=spawn_failed app=" << process.manifest.name
                  << " error=" << std::strerror(result) << '\n';
        return;
    }
    process.pid = static_cast<int>(pid);
    process.state = ProcessState::Starting;
    process.started_at_ms = monotonic_ms();
    process.last_heartbeat_ms = 0;
    std::cout << "ts_ms=" << process.started_at_ms << " event=started app=" << process.manifest.name
              << " pid=" << process.pid
              << " restart_count=" << process.restart_count << '\n';
}

void ProcessSupervisor::receive_heartbeats(std::uint64_t now_ms) {
    char buffer[129]{};
    ssize_t length = 0;
    while ((length = recv(heartbeat_fd_, buffer, 128, 0)) > 0) {
        try {
            const auto message = parse_heartbeat(std::string(buffer, static_cast<std::size_t>(length)));
            const auto process_it = processes_.find(message.application);
            if (process_it == processes_.end() || process_it->second.pid != message.pid) {
                std::cerr << "ts_ms=" << now_ms
                          << " event=heartbeat_rejected reason=identity_mismatch app="
                          << message.application << " pid=" << message.pid << '\n';
                continue;
            }
            auto& process = process_it->second;
            if (process.state != ProcessState::Starting && process.state != ProcessState::Ready) {
                std::cerr << "ts_ms=" << now_ms
                          << " event=heartbeat_rejected reason=invalid_state app="
                          << message.application << '\n';
                continue;
            }
            process.last_heartbeat_ms = now_ms;
            if (message.type == HeartbeatMessageType::Ready && process.state == ProcessState::Starting) {
                process.state = ProcessState::Ready;
                std::cout << "ts_ms=" << now_ms << " event=ready app=" << process.manifest.name
                          << " pid=" << process.pid << '\n';
            }
        } catch (const std::exception& error) {
            std::cerr << "ts_ms=" << now_ms
                      << " event=heartbeat_rejected reason=malformed detail=" << error.what() << '\n';
        }
    }
}

void ProcessSupervisor::enforce_health_deadlines(std::uint64_t now_ms) {
    for (auto& [name, process] : processes_) {
        (void)name;
        bool timed_out = false;
        const char* reason = "";
        if (process.state == ProcessState::Starting &&
            now_ms - process.started_at_ms > process.manifest.start_timeout_ms) {
            timed_out = true;
            reason = "start_timeout";
        } else if (process.state == ProcessState::Ready &&
                   now_ms - process.last_heartbeat_ms > process.manifest.heartbeat_timeout_ms) {
            timed_out = true;
            reason = "heartbeat_timeout";
        }
        if (timed_out) {
            process.state = ProcessState::Unhealthy;
            std::cerr << "ts_ms=" << now_ms << " event=unhealthy app="
                      << process.manifest.name << " reason=" << reason << '\n';
            if (kill(process.pid, SIGKILL) == -1 && errno != ESRCH) {
                std::cerr << "ts_ms=" << now_ms << " event=kill_failed app=" << process.manifest.name
                          << " error=" << std::strerror(errno) << '\n';
            }
        }
    }
}

void ProcessSupervisor::reap_children(std::uint64_t now_ms) {
    int status = 0;
    pid_t pid = -1;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (auto& [name, process] : processes_) {
            (void)name;
            if (process.pid == static_cast<int>(pid)) {
                handle_exit(process, status, now_ms);
                break;
            }
        }
    }
}

void ProcessSupervisor::handle_exit(ManagedProcess& process, int wait_status, std::uint64_t now_ms) {
    const bool exited = WIFEXITED(wait_status);
    const int code = exited ? WEXITSTATUS(wait_status) : -1;
    const int signal = WIFSIGNALED(wait_status) ? WTERMSIG(wait_status) : 0;
    const bool failure = !exited || code != 0;
    std::cout << "ts_ms=" << now_ms << " event=exited app=" << process.manifest.name
              << " pid=" << process.pid
              << " exit_code=" << code << " signal=" << signal << '\n';
    process.pid = -1;

    const bool policy_allows = process.manifest.restart_policy == RestartPolicy::Always ||
        (process.manifest.restart_policy == RestartPolicy::OnFailure && failure);
    if (policy_allows && process.restart_count < process.manifest.max_restarts) {
        const auto delay = backoff_ms(process);
        ++process.restart_count;
        process.restart_at_ms = now_ms + delay;
        process.state = ProcessState::Backoff;
        std::cout << "ts_ms=" << now_ms << " event=restart_scheduled app=" << process.manifest.name
                  << " delay_ms=" << delay << " restart_count=" << process.restart_count << '\n';
    } else {
        process.state = failure ? ProcessState::Failed : ProcessState::Stopped;
    }
}

bool ProcessSupervisor::all_terminal() const {
    for (const auto& [name, process] : processes_) {
        (void)name;
        if (process.state == ProcessState::Pending || process.state == ProcessState::Starting ||
            process.state == ProcessState::Ready || process.state == ProcessState::Unhealthy ||
            process.state == ProcessState::Backoff) return false;
    }
    return true;
}

}  // namespace vecu

