#include "vecu/dependency_graph.hpp"

#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace vecu {

std::vector<std::string> startup_order(const std::vector<ApplicationManifest>& applications) {
    enum class Visit { Unseen, Visiting, Done };
    std::unordered_map<std::string, const ApplicationManifest*> by_name;
    std::unordered_map<std::string, Visit> visits;
    for (const auto& app : applications) {
        if (!by_name.emplace(app.name, &app).second) {
            throw std::runtime_error("duplicate application in dependency graph: " + app.name);
        }
        visits.emplace(app.name, Visit::Unseen);
    }

    std::vector<std::string> order;
    std::function<void(const std::string&)> visit = [&](const std::string& name) {
        auto& state = visits.at(name);
        if (state == Visit::Done) return;
        if (state == Visit::Visiting) {
            throw std::runtime_error("dependency cycle contains application: " + name);
        }
        state = Visit::Visiting;
        for (const auto& dependency : by_name.at(name)->dependencies) {
            if (!by_name.contains(dependency)) {
                throw std::runtime_error("unknown dependency: " + dependency);
            }
            visit(dependency);
        }
        state = Visit::Done;
        order.push_back(name);
    };

    for (const auto& app : applications) visit(app.name);
    return order;
}

}  // namespace vecu

