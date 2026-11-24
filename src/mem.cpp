#include <iostream>
#include <fstream>
#include <string>

int main() {
    std::ifstream mem("/proc/meminfo");
    if (!mem.is_open()) {
        std::cerr << "Cannot open /proc/meminfo\n";
        return 1;
    }

    std::string line;
    int shown = 0;
    while (std::getline(mem, line) && shown < 4) {
        std::cout << line << "\n";
        shown++;
    }
    return 0;
}
