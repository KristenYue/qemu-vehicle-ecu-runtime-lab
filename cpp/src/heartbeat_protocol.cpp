#include "vecu/heartbeat_protocol.hpp"

#include <charconv>
#include <stdexcept>
#include <string_view>

namespace vecu {
namespace {

bool valid_name(std::string_view name) {
    if (name.empty() || name.size() > 64) return false;
    for (const char character : name) {
        const bool valid = (character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z') ||
                           (character >= '0' && character <= '9') ||
                           character == '-' || character == '_';
        if (!valid) return false;
    }
    return true;
}

}  // namespace

std::string encode_heartbeat(const HeartbeatMessage& message) {
    if (!valid_name(message.application) || message.pid <= 0) {
        throw std::invalid_argument("invalid heartbeat fields");
    }
    const auto type = message.type == HeartbeatMessageType::Ready ? "READY" : "HEARTBEAT";
    return std::string(type) + " " + message.application + " " + std::to_string(message.pid);
}

HeartbeatMessage parse_heartbeat(const std::string& payload) {
    if (payload.size() > 128 || payload.find('\0') != std::string::npos) {
        throw std::runtime_error("invalid heartbeat payload length");
    }
    const auto first = payload.find(' ');
    const auto second = first == std::string::npos ? std::string::npos : payload.find(' ', first + 1);
    if (first == std::string::npos || second == std::string::npos || payload.find(' ', second + 1) != std::string::npos) {
        throw std::runtime_error("heartbeat must contain exactly three fields");
    }

    HeartbeatMessage message{};
    const auto type = payload.substr(0, first);
    if (type == "READY") message.type = HeartbeatMessageType::Ready;
    else if (type == "HEARTBEAT") message.type = HeartbeatMessageType::Heartbeat;
    else throw std::runtime_error("unknown heartbeat message type");

    message.application = payload.substr(first + 1, second - first - 1);
    if (!valid_name(message.application)) throw std::runtime_error("invalid heartbeat application name");

    const auto pid_text = std::string_view(payload).substr(second + 1);
    const auto* begin = pid_text.data();
    const auto* end = begin + pid_text.size();
    const auto [ptr, error] = std::from_chars(begin, end, message.pid);
    if (error != std::errc{} || ptr != end || message.pid <= 0) {
        throw std::runtime_error("invalid heartbeat pid");
    }
    return message;
}

}  // namespace vecu
