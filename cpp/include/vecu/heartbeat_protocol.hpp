#pragma once

#include <string>

namespace vecu {

enum class HeartbeatMessageType { Ready, Heartbeat };

struct HeartbeatMessage {
    HeartbeatMessageType type;
    std::string application;
    int pid;
};

std::string encode_heartbeat(const HeartbeatMessage& message);
HeartbeatMessage parse_heartbeat(const std::string& payload);

}  // namespace vecu

