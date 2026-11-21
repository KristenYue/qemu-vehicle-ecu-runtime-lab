#include "vecu/heartbeat_protocol.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

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

    ~HeartbeatClient() {
        if (socket_fd_ != -1) close(socket_fd_);
    }

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

bool first_attempt(const std::filesystem::path& state_path) {
    if (std::filesystem::exists(state_path)) return false;
    std::ofstream marker(state_path, std::ios::trunc);
    if (!marker) throw std::runtime_error("cannot create fault state file");
    marker << getpid() << '\n';
    marker.flush();
    if (!marker) throw std::runtime_error("cannot persist fault state file");
    return true;
}

void run_healthy(const HeartbeatClient& client) {
    client.send(vecu::HeartbeatMessageType::Ready);
    std::cout << "service=faultable state=ready\n";
    for (int index = 0; index < 6; ++index) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        client.send(vecu::HeartbeatMessageType::Heartbeat);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: vecu_faultable_service <crash-once|hang-once> <state-file>\n";
        return 64;
    }
    try {
        const std::string mode = argv[1];
        const std::filesystem::path state_path = argv[2];
        HeartbeatClient client;
        const bool first = first_attempt(state_path);

        if (mode == "crash-once" && first) {
            std::cerr << "service=faultable state=injected_crash\n";
            return 42;
        }
        if (mode == "hang-once" && first) {
            client.send(vecu::HeartbeatMessageType::Ready);
            std::cerr << "service=faultable state=injected_hang\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
            return 0;
        }
        if (mode != "crash-once" && mode != "hang-once") return 64;
        run_healthy(client);
        std::cout << "service=faultable state=completed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "faultable service failed: " << error.what() << '\n';
        return 70;
    }
}

