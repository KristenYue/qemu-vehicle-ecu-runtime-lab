#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

#include "vecu/heartbeat_protocol.hpp"

namespace {

void send_message(int socket_fd, const sockaddr_un& address, vecu::HeartbeatMessageType type,
                  const std::string& application) {
    const auto payload = vecu::encode_heartbeat({type, application, static_cast<int>(getpid())});
    if (sendto(socket_fd, payload.data(), payload.size(), 0,
               reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) {
        throw std::runtime_error("heartbeat send failed");
    }
}

}  // namespace

int main() {
    const char* socket_path = std::getenv("VECU_HEARTBEAT_SOCKET");
    const char* app_name = std::getenv("VECU_APP_NAME");
    if (socket_path == nullptr || app_name == nullptr) return 64;
    const int socket_fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (socket_fd == -1) return 70;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (std::strlen(socket_path) >= sizeof(address.sun_path)) return 64;
    std::strncpy(address.sun_path, socket_path, sizeof(address.sun_path) - 1);

    send_message(socket_fd, address, vecu::HeartbeatMessageType::Ready, app_name);
    std::cout << "service=health state=ready\n";
    for (int index = 0; index < 6; ++index) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        send_message(socket_fd, address, vecu::HeartbeatMessageType::Heartbeat, app_name);
    }
    std::cout << "service=health state=stopped\n";
    close(socket_fd);
    return 0;
}

