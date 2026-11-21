#include <cstdlib>
#include <iostream>
#include <string>

int main() {
    const char* fail = std::getenv("VECU_CLIENT_FAIL");
    if (fail != nullptr && std::string(fail) == "1") {
        std::cerr << "service=vehicle-client state=injected_failure\n";
        return 42;
    }
    std::cout << "service=vehicle-client state=completed\n";
    return 0;
}

