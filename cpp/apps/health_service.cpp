#include <chrono>
#include <iostream>
#include <thread>

int main() {
    std::cout << "service=health state=ready\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(750));
    std::cout << "service=health state=stopped\n";
    return 0;
}

