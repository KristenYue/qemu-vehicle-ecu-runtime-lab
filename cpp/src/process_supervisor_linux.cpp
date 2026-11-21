#include "vecu/process_supervisor.hpp"

#include "vecu/dependency_graph.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <spawn.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
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
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (signal_fd_ == -1 || timer_fd_ == -1 || epoll_fd_ == -1) {
        throw std::runtime_error("failed to create Linux event-loop descriptors");
    }

    itimerspec timer{};
    timer.it_interval.tv_nsec = 100'000'000;
    timer.it_value = timer.it_interval;
    if (timerfd_settime(timer_fd_, 0, &timer, nullptr) == -1) {
        throw std::runtime_error("timerfd_settime failed");
    }
    add_epoll(epoll_fd_, signal_fd_);
    add_epoll(epoll_fd_, timer_fd_);

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
            }
        }
        start_ready_processes(monotonic_ms());
    }
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
            std::cerr << "event=dependency_failed app=" << process.manifest.name << '\n';
        } else if (dependencies_running(process)) {
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

bool ProcessSupervisor::dependencies_running(const ManagedProcess& process) const {
    for (const auto& dependency : process.manifest.dependencies) {
        if (processes_.at(dependency).state != ProcessState::Running) return false;
    }
    return true;
}

void ProcessSupervisor::spawn(ManagedProcess& process) {
    std::vector<char*> arguments;
    arguments.reserve(process.manifest.command.size() + 1);
    for (auto& item : process.manifest.command) arguments.push_back(item.data());
    arguments.push_back(nullptr);

    pid_t pid = -1;
    const int result = posix_spawnp(&pid, arguments.front(), nullptr, nullptr, arguments.data(), environ);
    if (result != 0) {
        process.state = ProcessState::Failed;
        std::cerr << "event=spawn_failed app=" << process.manifest.name
                  << " error=" << std::strerror(result) << '\n';
        return;
    }
    process.pid = static_cast<int>(pid);
    process.state = ProcessState::Running;
    std::cout << "event=started app=" << process.manifest.name << " pid=" << process.pid
              << " restart_count=" << process.restart_count << '\n';
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
    std::cout << "event=exited app=" << process.manifest.name << " pid=" << process.pid
              << " exit_code=" << code << " signal=" << signal << '\n';
    process.pid = -1;

    const bool policy_allows = process.manifest.restart_policy == RestartPolicy::Always ||
        (process.manifest.restart_policy == RestartPolicy::OnFailure && failure);
    if (policy_allows && process.restart_count < process.manifest.max_restarts) {
        const auto delay = backoff_ms(process);
        ++process.restart_count;
        process.restart_at_ms = now_ms + delay;
        process.state = ProcessState::Backoff;
        std::cout << "event=restart_scheduled app=" << process.manifest.name
                  << " delay_ms=" << delay << " restart_count=" << process.restart_count << '\n';
    } else {
        process.state = failure ? ProcessState::Failed : ProcessState::Stopped;
    }
}

bool ProcessSupervisor::all_terminal() const {
    for (const auto& [name, process] : processes_) {
        (void)name;
        if (process.state == ProcessState::Pending || process.state == ProcessState::Running ||
            process.state == ProcessState::Backoff) return false;
    }
    return true;
}

}  // namespace vecu

