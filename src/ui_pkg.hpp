#pragma once
#include "conf_boot.hpp"
#include <cstdlib>

inline void cmd_install_pkg(const std::vector<std::string>& a, const std::string& termux_home, OsConfig& cfg) {
    if (a.size() < 2) { std::cout << "Usage: install <package>\n"; return; }
    std::string pkg = a[1];
    std::string url = "http://127.0.0.1:8080/" + pkg + ".zip";
    std::string tmp = cfg.sys_root + "/var/tmp_pkg_" + pkg;
    std::string zip_p = tmp + "/" + pkg + ".zip";
    std::string dst = cfg.bin_dir + "/" + pkg;

    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);

    std::cout << "[install] Fetching " << url << " ...\n";
    if (std::system(("curl -fsSL \"" + url + "\" -o \"" + zip_p + "\"").c_str()) != 0 || !fs::exists(zip_p)) {
        std::cout << "[install] Download failed!\n";
        fs::remove_all(tmp, ec);
        return;
    }
    if (std::system(("unzip -qo \"" + zip_p + "\" -d \"" + tmp + "\"").c_str()) != 0) {
        std::cout << "[install] Unzip failed!\n";
        fs::remove_all(tmp, ec);
        return;
    }

    fs::path found;
    for (const auto& e : fs::recursive_directory_iterator(tmp, ec)) {
        if (e.is_regular_file(ec) && e.path() != zip_p) {
            if (e.path().filename() == pkg || found.empty()) {
                found = e.path();
                if (e.path().filename() == pkg) break;
            }
        }
    }
    if (found.empty()) {
        std::cout << "[install] Binary not found in archive!\n";
        fs::remove_all(tmp, ec);
        return;
    }

    fs::copy_file(found, dst, fs::copy_options::overwrite_existing, ec);
    fs::permissions(dst, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec, fs::perm_options::replace, ec);
    fs::remove_all(tmp, ec);

    if (std::find(cfg.installed_pkgs.begin(), cfg.installed_pkgs.end(), pkg) == cfg.installed_pkgs.end()) {
        cfg.installed_pkgs.push_back(pkg);
    }
    cfg.removed_pkgs.erase(std::remove(cfg.removed_pkgs.begin(), cfg.removed_pkgs.end(), pkg), cfg.removed_pkgs.end());
    cfg.cmds[pkg] = {pkg, "1.0", dst};
    save_os_conf(termux_home, cfg);

    std::cout << "[install] Installed '" << pkg << "' -> " << dst << " (+x) & updated os.conf\n";
}

inline void cmd_gui_tui(const OsConfig& cfg) {
    int sel = 0;
    while (true) {
        std::error_code ec;
        std::string cur = fs::current_path(ec).string();
        std::cout << "\033[2J\033[1;1H+------------------------------------------+\n";
        std::cout << "|         " << cfg.os_name << " Desktop (v" << cfg.os_version << ")         |\n";
        std::cout << "+------------------------------------------+\n";
        const char* items[4] = {"1. Memory Info (mem)", "2. System Info (sysinfo)", "3. Package List (pkglist)", "4. Back to Shell"};
        for (int i = 0; i < 4; ++i) {
            std::string row = std::string("| ") + (i == sel ? "> " : "  ") + items[i];
            if (row.size() < 43) row += std::string(43 - row.size(), ' ');
            std::cout << row << "|\n";
        }
        std::cout << "+------------------------------------------+\nMove (w/s or 1-4): ";
        std::string ch;
        if (!std::getline(std::cin, ch)) break;
        if (ch == "w" || ch == "W") { sel = (sel + 3) % 4; continue; }
        if (ch == "s" || ch == "S") { sel = (sel + 1) % 4; continue; }
        if (ch == "1") sel = 0; else if (ch == "2") sel = 1; else if (ch == "3") sel = 2; else if (ch == "4") break;
        else if (!ch.empty()) continue;

        std::cout << "\033[2J\033[1;1H";
        if (sel == 0) std::system(("\"" + cfg.bin_dir + "/mem\"").c_str());
        else if (sel == 1) std::system(("\"" + cfg.bin_dir + "/sysinfo\"").c_str());
        else if (sel == 2) {
            for (const auto& p : cfg.installed_pkgs) std::cout << p << " ";
            std::cout << "\n";
        }
        std::cout << "\nPress Enter...";
        std::string tmp; std::getline(std::cin, tmp);
    }
    std::cout << "\033[2J\033[1;1H";
}
