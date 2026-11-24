#pragma once
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <unistd.h>

namespace fs = std::filesystem;

inline std::string b64_encode(const std::string& in) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(T[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(T[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

inline std::string b64_decode(const std::string& in) {
    static int T[256];
    static bool init = false;
    if (!init) {
        std::fill(std::begin(T), std::end(T), -1);
        const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) T[(unsigned char)chars[i]] = i;
        init = true;
    }
    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c == '=' || T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

inline std::string session_store_dir(const std::string& home) {
    return home + "/myos/.sessions/.store";
}

inline std::vector<std::string> list_session_files(const std::string& home) {
    std::vector<std::string> files;
    std::string dir = session_store_dir(home);
    std::error_code ec;
    if (!fs::exists(dir, ec)) return files;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        std::string fn = e.path().filename().string();
        if (fn.rfind(".session", 0) == 0 && fn.size() >= 2 && fn.substr(fn.size() - 2) == ".s") {
            files.push_back(e.path().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

inline std::string next_session_path(const std::string& home) {
    std::string dir = session_store_dir(home);
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::string p1 = dir + "/.session.s";
    if (!fs::exists(p1, ec)) return p1;
    int idx = 2;
    while (true) {
        std::string cand = dir + "/.session" + std::to_string(idx) + ".s";
        if (!fs::exists(cand, ec)) return cand;
        ++idx;
    }
}

inline bool write_session_file(const std::string& path, const std::string& cwd, const std::string& last_cmd, bool force_first = false) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::string target = force_first ? (fs::path(path).parent_path().string() + "/.session.s") : path;
    std::string payload = "CWD=" + cwd + "\nLAST_CMD=" + last_cmd + "\n";
    std::ofstream ofs(target, std::ios::trunc);
    if (!ofs.is_open()) return false;
    ofs << b64_encode(payload) << "\n";
    return true;
}

inline bool apply_session_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return false;
    std::string b64_data, line;
    while (std::getline(ifs, line)) b64_data += line;
    std::string raw = b64_decode(b64_data);
    std::istringstream iss(raw);
    std::string cwd_val;
    while (std::getline(iss, line)) {
        if (line.rfind("CWD=", 0) == 0) cwd_val = line.substr(4);
    }
    if (!cwd_val.empty()) {
        std::error_code ec;
        fs::current_path(cwd_val, ec);
    }
    std::cout << "\033[1;32m[SESSION] Restored from " << fs::path(path).filename().string() << "\033[0m\n";
    return true;
}

inline void prompt_save_session_cmd(const std::string& home, const std::string& cwd, const std::string& last_cmd) {
    std::cout << "Save current session to encrypted .s file? [Y/N]: ";
    std::string ans;
    if (!std::getline(std::cin, ans)) return;
    if (ans == "Y" || ans == "y") {
        std::string dst = next_session_path(home);
        if (write_session_file(dst, cwd, last_cmd, false)) {
            std::cout << "\033[1;32m[SESSION] Saved -> " << fs::path(dst).filename().string() << "\033[0m\n";
        } else {
            std::cout << "[SESSION] Failed to save session file.\n";
        }
    } else {
        std::cout << "[SESSION] Cancelled.\n";
    }
}

inline void set_restore_once_flag(const std::string& home, bool enable) {
    std::string flag = session_store_dir(home) + "/.restore_once";
    std::error_code ec;
    fs::create_directories(session_store_dir(home), ec);
    if (enable) {
        std::ofstream ofs(flag, std::ios::trunc);
        ofs << "1\n";
    } else {
        fs::remove(flag, ec);
    }
}

inline bool has_restore_once_flag(const std::string& home) {
    std::error_code ec;
    return fs::exists(session_store_dir(home) + "/.restore_once", ec);
}

inline bool is_session_menu_disabled(const std::string& home) {
    std::error_code ec;
    return fs::exists(session_store_dir(home) + "/.skip_menu", ec);
}

inline void set_session_menu_disabled(const std::string& home, bool skip) {
    std::error_code ec;
    fs::create_directories(session_store_dir(home), ec);
    std::string f = session_store_dir(home) + "/.skip_menu";
    if (skip) { std::ofstream ofs(f, std::ios::trunc); ofs << "1\n"; }
    else fs::remove(f, ec);
}

inline void boot_session_menu(const std::string& home) {
    auto files = list_session_files(home);
    if (files.empty()) return;

    if (has_restore_once_flag(home)) {
        set_restore_once_flag(home, false);
        std::cout << "[SESSION] Auto-restoring previous session...\n";
        usleep(400000);
        apply_session_file(files.front());
        return;
    }

    std::cout << "\n=== Saved Sessions ===\n";
    for (size_t i = 0; i < files.size(); ++i) {
        std::cout << "  " << (i + 1) << ") " << fs::path(files[i]).filename().string() << "\n";
    }
    std::cout << "Select session number (or 0/Enter for new session): ";
    std::string sel;
    if (!std::getline(std::cin, sel) || sel.empty() || sel == "0") {
        std::cout << "[SESSION] Starting fresh session.\n";
        return;
    }
    int idx = std::atoi(sel.c_str());
    if (idx < 1 || static_cast<size_t>(idx) > files.size()) {
        std::cout << "[SESSION] Starting fresh session.\n";
        return;
    }
    std::cout << "Restore session '" << fs::path(files[idx - 1]).filename().string() << "'? [Y/N]: ";
    std::string yn;
    if (!std::getline(std::cin, yn)) return;
    if (yn == "Y" || yn == "y") {
        std::cout << "Please wait...\n";
        usleep(500000);
        apply_session_file(files[idx - 1]);
    } else {
        std::cout << "[SESSION] Starting fresh session.\n";
    }
}

inline std::string detect_file_type(const unsigned char* buf, size_t len) {
    if (len >= 4 && buf[0] == 0x7f && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F')
        return "ELF binary / executable";
    if (len >= 2 && buf[0] == 'P' && buf[1] == 'K')
        return "ZIP archive (PK header)";
    if (len >= 4 && buf[0] == 'd' && buf[1] == 'e' && buf[2] == 'x' && buf[3] == '\n')
        return "DEX bytecode file";
    if (len >= 2 && buf[0] == '#' && buf[1] == '!')
        return "Script with shebang (#!)";
    if (len >= 4 && buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G')
        return "PNG image";
    return "Raw / unknown binary or text";
}

inline void cmd_hexdump(const std::string& path, size_t max_lines = 8) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        std::cout << "hexdump: cannot open '" << path << "'\n";
        return;
    }
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    std::cout << "File   : " << path << " (" << data.size() << " bytes)\n";
    std::cout << "Header : ";
    size_t hdr_n = std::min<size_t>(data.size(), 8);
    for (size_t i = 0; i < hdr_n; ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
    }
    std::cout << std::dec << "\nType   : " << detect_file_type(data.data(), data.size()) << "\n---\n";

    size_t lines = 0;
    for (size_t off = 0; off < data.size() && lines < max_lines; off += 16, ++lines) {
        std::cout << std::hex << std::setw(8) << std::setfill('0') << off << "  ";
        for (size_t i = 0; i < 16; ++i) {
            if (off + i < data.size())
                std::cout << std::setw(2) << std::setfill('0') << (int)data[off + i] << " ";
            else
                std::cout << "   ";
            if (i == 7) std::cout << " ";
        }
        std::cout << " |";
        for (size_t i = 0; i < 16 && off + i < data.size(); ++i) {
            unsigned char c = data[off + i];
            std::cout << (c >= 32 && c <= 126 ? (char)c : '.');
        }
        std::cout << "|\n" << std::dec;
    }
}
