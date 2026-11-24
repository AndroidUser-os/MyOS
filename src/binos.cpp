#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>

namespace fs = std::filesystem;

static bool is_elf_file(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec) || !fs::is_regular_file(p, ec)) return false;
    auto sz = fs::file_size(p, ec);
    if (ec || sz < 4) return false;
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs.is_open()) return false;
    unsigned char hdr[4] = {0, 0, 0, 0};
    ifs.read(reinterpret_cast<char*>(hdr), 4);
    return ifs.gcount() == 4 &&
           hdr[0] == 0x7F && hdr[1] == 'E' && hdr[2] == 'L' && hdr[3] == 'F';
}

static bool has_exec_perm(const fs::path& p) {
    struct stat st{};
    if (stat(p.c_str(), &st) != 0) return false;
    return (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0;
}

static std::string find_os_conf(const std::string& termux_home) {
    const std::string candidates[] = {
        termux_home + "/myos/etc/os.conf",
        termux_home + "/myos/os.conf",
        termux_home + "/myos/conf/os.conf"
    };
    std::error_code ec;
    for (const auto& c : candidates) {
        if (fs::exists(c, ec) && fs::is_regular_file(c, ec)) return c;
    }
    return "";
}

static std::string read_bin_dir_from_conf(const std::string& conf_path, const std::string& termux_home) {
    std::string def_bin = termux_home + "/myos/bin";
    if (conf_path.empty()) return def_bin;
    std::ifstream ifs(conf_path);
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.rfind("bin_dir=", 0) == 0) {
            std::string val = line.substr(8);
            if (!val.empty() && val[0] == '~') {
                val = termux_home + val.substr(1);
            }
            if (!val.empty()) return val;
        }
    }
    return def_bin;
}

int main(int argc, char* argv[]) {
    const char* h = std::getenv("HOME");
    std::string termux_home = h ? h : "/data/data/com.termux/files/home";
    fs::path binos_path = fs::path(termux_home) / "myos" / "bin" / "binos";

    if (!is_elf_file(binos_path) || !has_exec_perm(binos_path)) {
        std::cout << "sh not executable\n";
        return 126;
    }

    if (argc < 2) {
        std::cout << "binos: missing target binary\n";
        return 1;
    }

    std::string conf_path = find_os_conf(termux_home);
    std::string conf_bin_dir = read_bin_dir_from_conf(conf_path, termux_home);

    std::string arg1 = argv[1];
    fs::path target(arg1);
    if (target.is_relative()) {
        target = fs::path(conf_bin_dir) / target.filename();
    }

    std::error_code ec;
    if (!fs::exists(target, ec) || !fs::is_regular_file(target, ec)) {
        std::cout << "Not found file on path in os.conf!\n";
        return 127;
    }

    if (!has_exec_perm(target) || !is_elf_file(target)) {
        std::cout << "sh not executable\n";
        return 126;
    }

    std::string cmd = "\"" + target.string() + "\"";
    for (int i = 2; i < argc; ++i) {
        cmd += " \"" + std::string(argv[i]) + "\"";
    }
    return std::system(cmd.c_str());
}
