#include "vecu/heartbeat_protocol.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

class HeartbeatClient {
public:
    HeartbeatClient() {
        const char* socket_path = std::getenv("VECU_HEARTBEAT_SOCKET");
        const char* application = std::getenv("VECU_APP_NAME");
        if (socket_path == nullptr || application == nullptr) {
            throw std::runtime_error("missing Execution Manager environment");
        }
        application_ = application;
        socket_fd_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (socket_fd_ == -1) throw std::runtime_error("cannot create heartbeat socket");
        address_.sun_family = AF_UNIX;
        if (std::strlen(socket_path) >= sizeof(address_.sun_path)) {
            throw std::runtime_error("heartbeat socket path is too long");
        }
        std::strncpy(address_.sun_path, socket_path, sizeof(address_.sun_path) - 1);
    }

    ~HeartbeatClient() { if (socket_fd_ != -1) close(socket_fd_); }

    void send(vecu::HeartbeatMessageType type) const {
        const auto payload = vecu::encode_heartbeat({type, application_, static_cast<int>(getpid())});
        if (sendto(socket_fd_, payload.data(), payload.size(), 0,
                   reinterpret_cast<const sockaddr*>(&address_), sizeof(address_)) == -1) {
            throw std::runtime_error("cannot send heartbeat");
        }
    }

private:
    int socket_fd_{-1};
    sockaddr_un address_{};
    std::string application_;
};

pid_t spawn_child(char** command, int output_fd) {
    const pid_t pid = fork();
    if (pid == -1) throw std::runtime_error("fork failed");
    if (pid == 0) {
        if (prctl(PR_SET_PDEATHSIG, SIGKILL) == -1) _exit(126);
        if (getppid() == 1) _exit(126);
        if (dup2(output_fd, STDOUT_FILENO) == -1 || dup2(output_fd, STDERR_FILENO) == -1) _exit(126);
        close(output_fd);
        execvp(command[0], command);
        _exit(127);
    }
    return pid;
}

int normalized_exit_status(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 125;
}

}  // namespace

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    if (argc < 4 || std::string(argv[2]) != "--") {
        std::cerr << "usage: vecu_managed_process_adapter <ready-token> -- <command> [args...]\n";
        return 64;
    }

    try {
        const std::string ready_token = argv[1];
        int output_pipe[2]{};
        if (pipe2(output_pipe, O_CLOEXEC | O_NONBLOCK) == -1) {
            throw std::runtime_error("pipe2 failed");
        }
        const pid_t child = spawn_child(&argv[3], output_pipe[1]);
        close(output_pipe[1]);
        std::cout << "adapter_event=child_started child_pid=" << child << '\n';

        HeartbeatClient heartbeat;
        bool ready = false;
        std::string buffered_output;
        auto next_heartbeat = std::chrono::steady_clock::now();

        while (true) {
            pollfd descriptor{output_pipe[0], POLLIN | POLLHUP, 0};
            (void)poll(&descriptor, 1, 50);
            if ((descriptor.revents & (POLLIN | POLLHUP)) != 0) {
                char buffer[512]{};
                ssize_t count = 0;
                while ((count = read(output_pipe[0], buffer, sizeof(buffer))) > 0) {
                    std::cout.write(buffer, count);
                    buffered_output.append(buffer, static_cast<std::size_t>(count));
                    if (!ready && buffered_output.find(ready_token) != std::string::npos) {
                        heartbeat.send(vecu::HeartbeatMessageType::Ready);
                        ready = true;
                        next_heartbeat = std::chrono::steady_clock::now();
                        std::cout << "adapter_event=child_ready child_pid=" << child << '\n';
                    }
                    if (buffered_output.size() > 4096) buffered_output.erase(0, 2048);
                }
            }

            const auto now = std::chrono::steady_clock::now();
            if (ready && now >= next_heartbeat) {
                heartbeat.send(vecu::HeartbeatMessageType::Heartbeat);
                next_heartbeat = now + std::chrono::milliseconds(100);
            }

            int status = 0;
            const pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child) {
                close(output_pipe[0]);
                const int code = normalized_exit_status(status);
                std::cout << "adapter_event=child_exited child_pid=" << child
                          << " normalized_code=" << code << '\n';
                return code;
            }
            if (waited == -1) throw std::runtime_error("waitpid failed");
        }
    } catch (const std::exception& error) {
        std::cerr << "managed process adapter failed: " << error.what() << '\n';
        return 70;
    }
}

