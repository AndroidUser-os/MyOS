#include "conf_boot.hpp"
#include "session_hex.hpp"
#include <cstdlib>

static void pause_enter() {
    std::cout << "\nPress Enter to continue...";
    std::string tmp; std::getline(std::cin, tmp);
}

static std::string home_session_dir(const std::string& termux_home) {
    std::string d = termux_home + "/myos/home/session";
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
}

static void toggle_session_menu(const std::string& termux_home) {
    bool cur = is_session_menu_disabled(termux_home);
    set_session_menu_disabled(termux_home, !cur);
    std::cout << (!cur ? "[MOD] Session selection menu is now DISABLED on boot.\n"
                      : "[MOD] Session selection menu is now ENABLED on boot.\n");
}

static void decrypt_session_flow(const std::string& termux_home) {
    auto files = list_session_files(termux_home);
    if (files.empty()) {
        std::cout << "[MOD] No .session*.s files found in .sessions/.store.\n";
        return;
    }
    std::cout << "=== Saved sessions (.s) ===\n";
    for (size_t i = 0; i < files.size(); ++i) {
        std::cout << "  " << (i + 1) << ") " << fs::path(files[i]).filename().string() << "\n";
    }
    std::cout << "Select number (0 to cancel): ";
    std::string num_s;
    if (!std::getline(std::cin, num_s)) return;
    int idx = std::atoi(num_s.c_str());
    if (idx <= 0 || static_cast<size_t>(idx) > files.size()) {
        std::cout << "[MOD] Cancelled.\n";
        return;
    }

    std::cout << "Please wait...\n";
    std::ifstream ifs(files[idx - 1]);
    std::string b64, line;
    while (std::getline(ifs, line)) b64 += line;
    std::string plain = b64_decode(b64);

    std::string out_dir = home_session_dir(termux_home);
    std::string stem = fs::path(files[idx - 1]).stem().string();
    std::string out_path = out_dir + "/" + stem + ".txt";

    std::ofstream ofs(out_path, std::ios::trunc);
    ofs << plain;
    ofs.close();

    std::cout << "[MOD] Decrypted successfully!\n";
    std::cout << "Saved to: " << out_path << "\n";
}

static std::string resolve_target_session_file(const std::string& termux_home, const std::string& input, const std::vector<std::string>& existing) {
    std::string store = session_store_dir(termux_home);
    int num = std::atoi(input.c_str());
    if (num >= 1 && static_cast<size_t>(num) <= existing.size()) {
        return existing[num - 1];
    }
    if (input == "session" || input == ".session" || input == ".session.s") {
        return store + "/.session.s";
    }
    std::string fn = input;
    if (fn.size() < 2 || fn.substr(fn.size() - 2) != ".s") {
        if (!fn.empty() && fn[0] != '.') fn = "." + fn;
        fn += ".s";
    } else if (!fn.empty() && fn[0] != '.') {
        fn = "." + fn;
    }
    return store + "/" + fn;
}

static void encrypt_session_flow(const std::string& termux_home) {
    std::string sess_dir = home_session_dir(termux_home);
    std::vector<std::string> txt_files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(sess_dir, ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == ".txt") {
            txt_files.push_back(e.path().string());
        }
    }
    std::sort(txt_files.begin(), txt_files.end());

    if (txt_files.empty()) {
        std::cout << "[MOD] No .txt session files found in " << sess_dir << "\n";
        return;
    }

    std::cout << "=== Decrypted sessions (.txt) ===\n";
    for (size_t i = 0; i < txt_files.size(); ++i) {
        std::cout << "  " << (i + 1) << ") " << fs::path(txt_files[i]).filename().string() << "\n";
    }
    std::cout << "Select number to encrypt to Base64 (.s) (0 to cancel): ";
    std::string num_s;
    if (!std::getline(std::cin, num_s)) return;
    int idx = std::atoi(num_s.c_str());
    if (idx <= 0 || static_cast<size_t>(idx) > txt_files.size()) {
        std::cout << "[MOD] Cancelled.\n";
        return;
    }

    std::cout << "Please wait...\n";
    std::ifstream ifs(txt_files[idx - 1]);
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string enc = b64_encode(ss.str());

    std::string stem = fs::path(txt_files[idx - 1]).stem().string();
    std::string home_s_path = sess_dir + "/" + stem + ".s";
    std::ofstream ofs(home_s_path, std::ios::trunc);
    ofs << enc << "\n";
    ofs.close();

    std::cout << "[MOD] Encrypted to Base64 (.s)!\n";
    std::cout << "Saved to: " << home_s_path << "\n";

    std::cout << "\nPut/replace in sessions folder (.sessions/.store)? [Y/N]: ";
    std::string ans;
    if (!std::getline(std::cin, ans)) return;
    if (ans == "Y" || ans == "y") {
        auto existing = list_session_files(termux_home);
        if (!existing.empty()) {
            std::cout << "Existing sessions in store:\n";
            for (size_t i = 0; i < existing.size(); ++i) {
                std::cout << "  " << (i + 1) << ") " << fs::path(existing[i]).filename().string() << "\n";
            }
        }
        std::cout << "Enter target session name (e.g. session) or number: ";
        std::string target_in;
        if (!std::getline(std::cin, target_in) || target_in.empty()) {
            std::cout << "[MOD] Skipped replacing in sessions folder. Kept in: " << home_s_path << "\n";
            return;
        }
        std::string dst_file = resolve_target_session_file(termux_home, target_in, existing);
        fs::create_directories(session_store_dir(termux_home), ec);
        std::ofstream dst_ofs(dst_file, std::ios::trunc);
        dst_ofs << enc << "\n";
        dst_ofs.close();
        std::cout << "[MOD] Replaced session in store: " << dst_file << "\n";
    } else {
        std::cout << "[MOD] Nothing replaced in sessions folder. Encrypted file is at: " << home_s_path << "\n";
    }
}

static void sessions_crypto_menu(const std::string& termux_home) {
    std::cout << "+----------------------------------------------+\n";
    std::cout << "|         Session Decrypt / Encrypt (.s)       |\n";
    std::cout << "+----------------------------------------------+\n";
    std::cout << "  1. Decrypt session (.s -> .txt in home/session)\n";
    std::cout << "  2. Encrypt session (.txt -> .s & replace)\n";
    std::cout << "  0. Back\n";
    std::cout << "Select: ";
    std::string ch;
    if (!std::getline(std::cin, ch)) return;
    std::cout << "\033[2J\033[1;1H";
    if (ch == "1") decrypt_session_flow(termux_home);
    else if (ch == "2") encrypt_session_flow(termux_home);
}

static void remove_cmd_via_mod(const std::string& termux_home) {
    OsConfig cfg;
    if (!boot_from_conf(termux_home, cfg)) return;
    std::cout << "Enter command/package name to remove: ";
    std::string name;
    if (!std::getline(std::cin, name) || name.empty()) return;

    std::error_code ec;
    fs::path bin_p = fs::path(cfg.bin_dir) / name;
    if (fs::exists(bin_p, ec)) fs::remove(bin_p, ec);
    cfg.installed_pkgs.erase(std::remove(cfg.installed_pkgs.begin(), cfg.installed_pkgs.end(), name), cfg.installed_pkgs.end());
    if (std::find(cfg.removed_pkgs.begin(), cfg.removed_pkgs.end(), name) == cfg.removed_pkgs.end()) {
        cfg.removed_pkgs.push_back(name);
    }
    cfg.cmds.erase(name);
    save_os_conf(termux_home, cfg);
    std::cout << "[MOD] Removed '" << name << "' and updated os.conf.\n";
}

int main() {
    const char* h = std::getenv("HOME");
    std::string termux_home = h ? h : "";
    int sel = 0;

    while (true) {
        bool menu_off = is_session_menu_disabled(termux_home);
        std::cout << "\033[2J\033[1;1H+----------------------------------------------+\n";
        std::cout << "|        MyOS System Modificator (C++)         |\n";
        std::cout << "+----------------------------------------------+\n";
        std::string opt1 = std::string("1. Session menu on boot: ") + (menu_off ? "OFF (skip)" : "ON (ask)");
        const char* opts[4] = {
            opt1.c_str(),
            "2. Decrypt / Encrypt sessions (.s <-> .txt)",
            "3. Remove a command / package",
            "4. Exit modificator"
        };
        for (int i = 0; i < 4; ++i) {
            std::string row = std::string("| ") + (i == sel ? "> " : "  ") + opts[i];
            if (row.size() < 47) row += std::string(47 - row.size(), ' ');
            std::cout << row << "|\n";
        }
        std::cout << "+----------------------------------------------+\nSelect (1-4, w/s, Enter): ";
        std::string in;
        if (!std::getline(std::cin, in)) break;
        if (in == "w" || in == "W") { sel = (sel + 3) % 4; continue; }
        if (in == "s" || in == "S") { sel = (sel + 1) % 4; continue; }
        if (in == "1") sel = 0;
        else if (in == "2") sel = 1;
        else if (in == "3") sel = 2;
        else if (in == "4") break;
        else if (!in.empty()) continue;

        std::cout << "\033[2J\033[1;1H";
        if (sel == 0) toggle_session_menu(termux_home);
        else if (sel == 1) sessions_crypto_menu(termux_home);
        else if (sel == 2) remove_cmd_via_mod(termux_home);
        pause_enter();
    }
    std::cout << "\033[2J\033[1;1H";
    return 0;
}
