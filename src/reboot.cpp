#include <iostream>
#include <string>
#include <cstdlib>
#include <unistd.h>

int main() {
    const char* h = std::getenv("HOME");
    std::string home = h ? h : "";
    std::string myos_bin = home + "/myos/bin/myos";
    std::cout << "\033[1;33m[REBOOT] Exiting session and restarting MyOS...\033[0m\n";
    usleep(300000);
    execl(myos_bin.c_str(), myos_bin.c_str(), (char*)nullptr);
    std::system(("\"" + myos_bin + "\"").c_str());
    return 0;
}
