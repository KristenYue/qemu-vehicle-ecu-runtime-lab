#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "vecu/heartbeat_protocol.hpp"

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
    const auto ready = vecu::encode_heartbeat(
        {vecu::HeartbeatMessageType::Ready, app_name, static_cast<int>(getpid())});
    if (sendto(socket_fd, ready.data(), ready.size(), 0,
               reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) return 70;

    const char* fail = std::getenv("VECU_CLIENT_FAIL");
    if (fail != nullptr && std::string(fail) == "1") {
        std::cerr << "service=vehicle-client state=injected_failure\n";
        return 42;
    }
    std::cout << "service=vehicle-client state=completed\n";
    close(socket_fd);
    return 0;
}

