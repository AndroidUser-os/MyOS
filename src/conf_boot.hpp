#pragma once
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <filesystem>

namespace fs = std::filesystem;

struct CmdMeta {
    std::string name;
    std::string ver;
    std::string path;
};

struct OsConfig {
    std::string os_name = "MyOS";
    std::string os_version = "0.9";
    std::string kernel_name = "kernel.bin";
    std::string kernel_version = "0.1";
    std::string kernel_arch = "aarch64";
    std::string kernel_path;
    std::string sys_root;
    std::string prompt_color = "green";
    std::string bin_dir;
    std::string apps_dir;
    std::string history_file;
    bool log_history = true;
    std::vector<std::string> installed_pkgs;
    std::vector<std::string> removed_pkgs;
    std::map<std::string, CmdMeta> cmds;
};

inline std::string expand_tilde(const std::string& p, const std::string& home) {
    if (home.empty()) return p;
    if (p == "~") return home;
    if (p.rfind("~/", 0) == 0) return home + p.substr(1);
    return p;
}

inline std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

inline std::string join_csv(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ",";
        out += v[i];
    }
    return out;
}

inline std::string read_raw(const std::string& p) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs.is_open()) return "";
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

inline std::string calc_sig(const std::string& p) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs.is_open()) return "";
    unsigned long long h = 14695981039346656037ULL, n = 0;
    char c;
    while (ifs.get(c)) { ++n; h ^= static_cast<unsigned char>(c); h *= 1099511628211ULL; }
    return std::to_string(n) + ":" + std::to_string(h);
}

inline void save_os_conf(const std::string& home, const OsConfig& cfg) {
    std::string conf_p = home + "/myos/etc/os.conf";
    std::ofstream ofs(conf_p, std::ios::trunc);
    if (!ofs.is_open()) return;
    ofs << "OS_NAME=" << cfg.os_name << "\n"
        << "OS_VERSION=" << cfg.os_version << "\n"
        << "KERNEL_NAME=" << cfg.kernel_name << "\n"
        << "KERNEL_VERSION=" << cfg.kernel_version << "\n"
        << "KERNEL_ARCH=" << cfg.kernel_arch << "\n"
        << "KERNEL_PATH=" << cfg.kernel_path << "\n"
        << "SYS_ROOT=" << cfg.sys_root << "\n"
        << "PROMPT_COLOR=" << cfg.prompt_color << "\n"
        << "BIN_DIR=" << cfg.bin_dir << "\n"
        << "APPS_DIR=" << cfg.apps_dir << "\n"
        << "HISTORY_FILE=" << cfg.history_file << "\n"
        << "LOG_HISTORY=" << (cfg.log_history ? "1" : "0") << "\n\n"
        << "INSTALLED_PKGS=" << join_csv(cfg.installed_pkgs) << "\n"
        << "REMOVED_PKGS=" << join_csv(cfg.removed_pkgs) << "\n\n";
    for (const auto& kv : cfg.cmds) {
        const std::string& k = kv.first;
        const CmdMeta& m = kv.second;
        ofs << "CMD_" << k << "_NAME=" << m.name << "\n"
            << "CMD_" << k << "_VER=" << m.ver << "\n"
            << "CMD_" << k << "_PATH=" << m.path << "\n\n";
    }
    ofs.flush();
    std::ofstream snap(home + "/myos/var/os.conf.prev", std::ios::binary | std::ios::trunc);
    snap << read_raw(conf_p);
}

inline bool boot_from_conf(const std::string& home, OsConfig& cfg) {
    std::string conf_p = home + "/myos/etc/os.conf";
    std::string prev_p = home + "/myos/var/os.conf.prev";
    cfg.kernel_path = home + "/myos/kernel/kernel.bin";
    cfg.sys_root = home + "/myos";
    cfg.bin_dir = home + "/myos/bin";
    cfg.apps_dir = home + "/myos/apps";
    cfg.history_file = home + "/myos/var/history.log";

    if (!fs::exists(conf_p)) {
        std::cerr << "\033[1;31m[FATAL ERROR] Failed to use kernel or locate operating system at configured path.\033[0m\n";
        return false;
    }

    std::string cur_raw = read_raw(conf_p);
    std::string prev_raw = read_raw(prev_p);
    if (!prev_raw.empty() && cur_raw != prev_raw) {
        std::cout << "\033[1;33m[WARN] os.conf was modified compared to previous snapshot.\033[0m\n";
    }

    std::map<std::string, std::string> kv;
    std::istringstream iss(cur_raw);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq != std::string::npos) kv[line.substr(0, eq)] = line.substr(eq + 1);
    }

    auto get_k = [&](const std::string& k, const std::string& d) {
        auto it = kv.find(k);
        return (it != kv.end() && !it->second.empty()) ? it->second : d;
    };

    cfg.os_name = get_k("OS_NAME", cfg.os_name);
    cfg.os_version = get_k("OS_VERSION", cfg.os_version);
    cfg.kernel_name = get_k("KERNEL_NAME", cfg.kernel_name);
    cfg.kernel_version = get_k("KERNEL_VERSION", cfg.kernel_version);
    cfg.kernel_arch = get_k("KERNEL_ARCH", cfg.kernel_arch);
    cfg.kernel_path = expand_tilde(get_k("KERNEL_PATH", cfg.kernel_path), home);
    cfg.sys_root = expand_tilde(get_k("SYS_ROOT", cfg.sys_root), home);
    cfg.prompt_color = get_k("PROMPT_COLOR", cfg.prompt_color);
    cfg.bin_dir = expand_tilde(get_k("BIN_DIR", cfg.bin_dir), home);
    cfg.apps_dir = expand_tilde(get_k("APPS_DIR", cfg.apps_dir), home);
    cfg.history_file = expand_tilde(get_k("HISTORY_FILE", cfg.history_file), home);
    cfg.log_history = (get_k("LOG_HISTORY", "1") != "0");

    cfg.installed_pkgs = split_csv(kv.count("INSTALLED_PKGS") ? kv["INSTALLED_PKGS"] : "");
    cfg.removed_pkgs = split_csv(kv.count("REMOVED_PKGS") ? kv["REMOVED_PKGS"] : "");

    for (const auto& it : kv) {
        if (it.first.rfind("CMD_", 0) == 0) {
            std::string rest = it.first.substr(4);
            auto u1 = rest.find('_');
            if (u1 == std::string::npos) continue;
            std::string cname = rest.substr(0, u1);
            std::string field = rest.substr(u1 + 1);
            if (field == "NAME") cfg.cmds[cname].name = it.second;
            else if (field == "VER") cfg.cmds[cname].ver = it.second;
            else if (field == "PATH") cfg.cmds[cname].path = expand_tilde(it.second, home);
        }
    }

    if (cfg.kernel_name != "kernel.bin") {
        std::cout << "\033[1;33m[WARN] Kernel name mismatch: '" << cfg.kernel_name << "' != 'kernel.bin'.\033[0m\n";
    }

    bool bad_path = !fs::exists(cfg.kernel_path) || !fs::is_regular_file(cfg.kernel_path) ||
                    !fs::exists(cfg.sys_root) || !fs::is_directory(cfg.sys_root);
    bool bad_ver = (cfg.os_version != "0.9" || cfg.kernel_version != "0.1");

    if (bad_path) {
        std::cerr << "\033[1;31m[FATAL ERROR] Failed to use kernel or locate operating system at configured path.\033[0m\n";
    }
    if (bad_ver) {
        std::cerr << "\033[1;31m[FATAL ERROR] Invalid version of OS and kernel.\033[0m\n";
    }
    if (bad_path || bad_ver) {
        std::cerr << "\033[1;31m[HALT] Halted.\033[0m\n";
        return false;
    }

    std::string actual = calc_sig(cfg.kernel_path);
    std::ifstream sig_in(home + "/myos/var/kernel.sig");
    std::string expected;
    if (actual.empty() || !(sig_in >> expected) || expected != actual) {
        std::cerr << "\033[1;31m[FATAL ERROR] Failed to use kernel or locate operating system at configured path.\033[0m\n";
        return false;
    }

    std::error_code ec;
    fs::create_directories(home + "/myos/var", ec);
    std::ofstream snap(prev_p, std::ios::binary | std::ios::trunc);
    snap << cur_raw;

    std::cout << "\033[1;32m[" << cfg.os_name << " v" << cfg.os_version << "] Kernel '"
              << cfg.kernel_name << "' v" << cfg.kernel_version << " (" << cfg.kernel_arch
              << ") -> " << cfg.kernel_path << "\033[0m\n";
    return true;
}
