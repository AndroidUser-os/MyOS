#include <termios.h>
#include <unistd.h>
#include "conf_boot.hpp"
#include "session_hex.hpp"
#include "ui_pkg.hpp"
#include <sstream>

static std::string fmt_dir(const std::string& cwd, const std::string& home) {
    if (!home.empty() && cwd == home) return "~";
    std::string pre = home + "/";
    if (!home.empty() && cwd.rfind(pre, 0) == 0) return "~" + cwd.substr(home.size());
    return cwd;
}

static bool virt_ls(const std::string& rel) {
    if (rel == "proc") { std::cout << "uptime  meminfo  shell\n"; return true; }
    if (rel == "sys")  { std::cout << "os_name  os_version  kernel_name  kernel_version  kernel_arch  sys_root\n"; return true; }
    if (rel == "dev")  { std::cout << "null  zero  random\n"; return true; }
    return false;
}

static bool virt_cat(const std::string& rel, const OsConfig& cfg, const std::string& cwd) {
    if (rel == "proc/uptime") {
        std::ifstream up("/proc/uptime"); double s = 0;
        if (up >> s) std::cout << static_cast<long>(s) << " sec\n";
        return true;
    }
    if (rel == "proc/shell") { std::cout << "pid=" << getpid() << "\ncwd=" << cwd << "\n"; return true; }
    if (rel == "sys/os_name")        { std::cout << cfg.os_name << "\n"; return true; }
    if (rel == "sys/os_version")     { std::cout << cfg.os_version << "\n"; return true; }
    if (rel == "sys/kernel_name")    { std::cout << cfg.kernel_name << "\n"; return true; }
    if (rel == "sys/kernel_version") { std::cout << cfg.kernel_version << "\n"; return true; }
    if (rel == "sys/kernel_arch")    { std::cout << cfg.kernel_arch << "\n"; return true; }
    if (rel == "sys/sys_root")       { std::cout << cfg.sys_root << "\n"; return true; }
    if (rel == "dev/null")           { return true; }
    if (rel == "dev/zero")           { std::cout << "0000000000000000\n"; return true; }
    if (rel == "dev/random") {
        unsigned int seed = static_cast<unsigned int>(getpid()) * 2654435761u;
        for (int i = 0; i < 8; ++i) { seed = seed * 1664525u + 1013904223u; std::cout << std::hex << ((seed >> 16) & 0xff); }
        std::cout << std::dec << "\n";
        return true;
    }
    return false;
}

static std::string virt_rel(const std::string& raw, const std::string& cwd, const std::string& termux_home) {
    fs::path p = raw.empty() ? fs::path(cwd) : (raw[0] == '/' ? fs::path(raw) : fs::path(cwd) / raw);
    std::string s = p.lexically_normal().string(), base = termux_home + "/myos/";
    return (s.rfind(base, 0) == 0) ? s.substr(base.size()) : "";
}

static void run_edit(const std::string& filename) {
    std::vector<std::string> lines;
    std::ifstream ifs(filename);
    std::string l;
    while (std::getline(ifs, l)) lines.push_back(l);
    std::cout << "=== edit: " << filename << " (:w save, :p print, :d <n> del, :q quit) ===\n";
    std::string in;
    while (true) {

        std::cout << "edit> ";
        if (!std::getline(std::cin, in)) break;
        if (in == ":q") break;
        else if (in == ":w") {
            std::ofstream ofs(filename, std::ios::trunc);
            for (const auto& x : lines) ofs << x << "\n";
            std::cout << "Saved (" << lines.size() << " lines).\n";
        } else if (in == ":p") {
            for (size_t i = 0; i < lines.size(); ++i) std::cout << (i + 1) << ": " << lines[i] << "\n";
        } else if (in == ":e") {
            if (lines.empty()) {
                std::cout << "Buffer is empty.\n";
                continue;
            }
            std::cout << "Line number (1-" << lines.size() << "): ";
            std::string num_s;
            if (!std::getline(std::cin, num_s)) break;
            int idx = std::atoi(num_s.c_str());
            if (idx < 1 || static_cast<size_t>(idx) > lines.size()) {
                std::cout << "Bad line number.\n";
                continue;
            }
            std::cout << "Current [" << idx << "]: " << lines[idx - 1] << "\n";
            std::cout << "New text: ";
            std::string new_line;
            if (!std::getline(std::cin, new_line)) break;
            std::cout << "Apply change? [Y/N]: ";
            std::string yn;
            if (!std::getline(std::cin, yn)) break;
            if (yn == "Y" || yn == "y") {
                lines[idx - 1] = new_line;
                std::cout << "Updated line " << idx << ".\n";
            } else {
                std::cout << "Discarded changes.\n";
            }
        } else if (in == ":e") {
            if (lines.empty()) {
                std::cout << "Buffer is empty.\n";
                continue;
            }
            std::cout << "Line number (1-" << lines.size() << "): ";
            std::string num_s;
            if (!std::getline(std::cin, num_s)) break;
            int idx = std::atoi(num_s.c_str());
            if (idx < 1 || static_cast<size_t>(idx) > lines.size()) {
                std::cout << "Bad line number.\n";
                continue;
            }
            std::cout << "Current [" << idx << "]: " << lines[idx - 1] << "\n";
            std::cout << "New text: ";
            std::string new_line;
            if (!std::getline(std::cin, new_line)) break;
            std::cout << "Apply change? [Y/N]: ";
            std::string yn;
            if (!std::getline(std::cin, yn)) break;
            if (yn == "Y" || yn == "y") {
                lines[idx - 1] = new_line;
                std::cout << "Updated line " << idx << ".\n";
            } else {
                std::cout << "Discarded changes.\n";
            }
        } else if (in.rfind(":d ", 0) == 0) {
            int idx = std::atoi(in.substr(3).c_str());
            if (idx >= 1 && static_cast<size_t>(idx) <= lines.size()) {
                lines.erase(lines.begin() + (idx - 1));
                std::cout << "Deleted line " << idx << ".\n";
            } else std::cout << "Bad line number.\n";
        } else lines.push_back(in);
    }
}

static bool check_cmd_meta(const OsConfig& cfg, const std::string& cmd, fs::path& resolved_bin) {
    auto it = cfg.cmds.find(cmd);
    if (it == cfg.cmds.end()) {
        resolved_bin = fs::path(cfg.bin_dir) / cmd;
        return fs::exists(resolved_bin) && fs::is_regular_file(resolved_bin);
    }
    const CmdMeta& m = it->second;
    if (!m.name.empty() && m.name != cmd)
        std::cout << "\033[1;33m[WARN] Command '" << cmd << "' name changed in os.conf to '" << m.name << "' (still running).\033[0m\n";
    if (!m.ver.empty() && m.ver != "1.0")
        std::cout << "\033[1;33m[WARN] Command '" << cmd << "' version changed in os.conf to '" << m.ver << "' (still running).\033[0m\n";
    resolved_bin = m.path.empty() ? (fs::path(cfg.bin_dir) / cmd) : fs::path(m.path);
    if (!fs::exists(resolved_bin) || !fs::is_regular_file(resolved_bin)) {
        std::cout << "\033[1;31m[WARN] Command '" << cmd << "' will not work: location changed or missing (" << resolved_bin.string() << ").\033[0m\n";
        return false;
    }
    return true;
}


static void render_line_input(const std::string& prompt, const std::string& buf, size_t pos, size_t& prev_len) {
    std::cout << "\r\033[K" << prompt << buf;
    if (buf.size() < prev_len) {
        std::cout << std::string(prev_len - buf.size(), ' ');
    }
    prev_len = buf.size();
    size_t tail = buf.size() - pos;
    if (tail > 0) {
        std::cout << "\033[" << tail << "D";
    }
    std::cout.flush();
}

static bool read_line_with_history(const std::string& prompt, const std::vector<std::string>& hist, std::string& out) {
    if (!isatty(STDIN_FILENO)) {
        return static_cast<bool>(std::getline(std::cin, out));
    }
    std::cout.flush();
    termios old_tio{};
    if (tcgetattr(STDIN_FILENO, &old_tio) != 0) {
        return static_cast<bool>(std::getline(std::cin, out));
    }
    termios raw = old_tio;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    std::string buf;
    size_t pos = 0;
    size_t prev_len = 0;
    int h_idx = static_cast<int>(hist.size());
    std::string saved_draft;
    bool eof_hit = false;

    render_line_input(prompt, buf, pos, prev_len);

    while (true) {
        unsigned char c = 0;
        ssize_t n = read(STDIN_FILENO, &c, 1);
        if (n <= 0) {
            eof_hit = true;
            break;
        }
        if (c == '\r' || c == '\n') {
            std::cout << "\n";
            std::cout.flush();
            break;
        }
        if (c == 4 && buf.empty()) {
            eof_hit = true;
            std::cout << "\n";
            std::cout.flush();
            break;
        }
        if (c == 127 || c == 8) {
            if (pos > 0) {
                buf.erase(pos - 1, 1);
                --pos;
                render_line_input(prompt, buf, pos, prev_len);
            }
            continue;
        }
        if (c == 27) {
            unsigned char seq[2] = {0, 0};
            if (read(STDIN_FILENO, &seq[0], 1) == 1 && seq[0] == '[') {
                if (read(STDIN_FILENO, &seq[1], 1) == 1) {
                    if (seq[1] == 'A') {
                        if (!hist.empty() && h_idx > 0) {
                            if (h_idx == static_cast<int>(hist.size())) saved_draft = buf;
                            --h_idx;
                            buf = hist[h_idx];
                            pos = buf.size();
                            render_line_input(prompt, buf, pos, prev_len);
                        }
                    } else if (seq[1] == 'B') {
                        if (h_idx < static_cast<int>(hist.size())) {
                            ++h_idx;
                            buf = (h_idx == static_cast<int>(hist.size())) ? saved_draft : hist[h_idx];
                            pos = buf.size();
                            render_line_input(prompt, buf, pos, prev_len);
                        }
                    } else if (seq[1] == 'C') {
                        if (pos < buf.size()) {
                            ++pos;
                            render_line_input(prompt, buf, pos, prev_len);
                        }
                    } else if (seq[1] == 'D') {
                        if (pos > 0) {
                            --pos;
                            render_line_input(prompt, buf, pos, prev_len);
                        }
                    } else if (seq[1] == 'H') {
                        pos = 0;
                        render_line_input(prompt, buf, pos, prev_len);
                    } else if (seq[1] == 'F') {
                        pos = buf.size();
                        render_line_input(prompt, buf, pos, prev_len);
                    } else if (seq[1] == '3') {
                        unsigned char tilde = 0;
                        read(STDIN_FILENO, &tilde, 1);
                        if (tilde == '~' && pos < buf.size()) {
                            buf.erase(pos, 1);
                            render_line_input(prompt, buf, pos, prev_len);
                        }
                    }
                }
            }
            continue;
        }
        if (c >= 32) {
            buf.insert(buf.begin() + pos, static_cast<char>(c));
            ++pos;
            render_line_input(prompt, buf, pos, prev_len);
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
    if (eof_hit) return false;
    out = buf;
    return true;
}


static bool binos_ready_for_exec(const std::string& termux_home) {
    fs::path bp = fs::path(termux_home) / "myos" / "bin" / "binos";
    std::error_code ec;
    if (!fs::exists(bp, ec) || !fs::is_regular_file(bp, ec)) return false;
    auto sz = fs::file_size(bp, ec);
    if (ec || sz < 4) return false;
    std::ifstream ifs(bp, std::ios::binary);
    if (!ifs.is_open()) return false;
    unsigned char hdr[4] = {0, 0, 0, 0};
    ifs.read(reinterpret_cast<char*>(hdr), 4);
    if (ifs.gcount() != 4 || hdr[0] != 0x7F || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F') {
        return false;
    }
    fs::perms perms = fs::status(bp, ec).permissions();
    if (ec) return false;
    using fs::perms;
    return (perms & (perms::owner_exec | perms::group_exec | perms::others_exec)) != perms::none;
}


static bool check_binos_gate(const std::string& termux_home) {
    fs::path bp = fs::path(termux_home) / "myos" / "bin" / "binos";
    std::error_code ec;
    if (!fs::exists(bp, ec) || !fs::is_regular_file(bp, ec)) return false;
    auto sz = fs::file_size(bp, ec);
    if (ec || sz < 4) return false;
    std::ifstream ifs(bp, std::ios::binary);
    if (!ifs.is_open()) return false;
    unsigned char hdr[4] = {0, 0, 0, 0};
    ifs.read(reinterpret_cast<char*>(hdr), 4);
    if (ifs.gcount() != 4 || hdr[0] != 0x7F || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F') {
        return false;
    }
    fs::perms perms = fs::status(bp, ec).permissions();
    if (ec) return false;
    using fs::perms;
    return (perms & (perms::owner_exec | perms::group_exec | perms::others_exec)) != perms::none;
}


static bool prompt_reserve_mode(const std::string& termux_home, bool& reserve_mode) {
    std::cout << "Switch to reserve commands mode from .resrevcommands? [y/n]: ";
    std::string ans;
    if (!std::getline(std::cin, ans)) return false;
    if (!ans.empty() && (ans[0] == 'y' || ans[0] == 'Y')) {
        reserve_mode = true;
        fs::create_directories(fs::path(termux_home) / "myos" / "bin" / ".resrevcommands");
        std::cout << "[RESERVE] Reserve commands mode enabled (.resrevcommands)\n";
        return true;
    }
    if (!ans.empty() && (ans[0] == 'n' || ans[0] == 'N')) {
        std::cout << "[RESERVE] Stayed in normal mode\n";
        return false;
    }
    return false;
}

static bool has_reserve_cmd(const std::string& termux_home, const std::string& cmd) {
    fs::path rp = fs::path(termux_home) / "myos" / "bin" / ".resrevcommands" / cmd;
    std::error_code ec;
    return fs::exists(rp, ec) && fs::is_regular_file(rp, ec);
}

int main() {
    const char* h = std::getenv("HOME");
    std::string termux_home = h ? h : "";
    std::string home = termux_home + "/myos/home";
    std::error_code init_ec;
    fs::create_directories(home, init_ec);
    fs::current_path(home, init_ec);

    OsConfig cfg;
    if (!boot_from_conf(termux_home, cfg)) return 1;
    boot_session_menu(termux_home);

    std::string col = (cfg.prompt_color == "cyan") ? "\033[1;36m" : "\033[1;32m";
    std::string line, last_cmd;
    std::cout << "Commands: help, uptime, mem, pwd, cd, ls (-a/-s), cat, touch, mkdir, rm, rmdir, edit, gui, install, hexdump, pkglist, remove installed <pkg>, save session, history, clear, exit\n";

    std::vector<std::string> cmd_history;
    if (!cfg.history_file.empty()) {
        std::string hpath = cfg.history_file;
        if (!hpath.empty() && hpath[0] == '~') {
            const char* th = std::getenv("HOME");
            if (th) hpath = std::string(th) + hpath.substr(1);
        }
        std::ifstream h_in(hpath);
        std::string h_line;
        while (std::getline(h_in, h_line)) {
            if (!h_line.empty()) cmd_history.push_back(h_line);
        }
    }
    std::string cur_prompt;
while (true) {
            {
                std::error_code p_ec;
                std::string p_cwd = fs::current_path(p_ec).string();
                cur_prompt = col + cfg.os_name + "\033[0m:\033[1;36m" + fmt_dir(p_cwd, home) + "\033[0m> ";
            }
        std::error_code ec;
        std::string cwd = fs::current_path(ec).string();
        std::cout << col << cfg.os_name << "\033[0m:\033[1;36m" << fmt_dir(cwd, home) << "\033[0m> ";
        if (!read_line_with_history(cur_prompt, cmd_history, line)) break;
        if (line.empty()) continue;
        last_cmd = line;
            if (cmd_history.empty() || cmd_history.back() != line) cmd_history.push_back(line);

        if (cfg.log_history) {
            std::ofstream log(cfg.history_file, std::ios::app);
            if (log.is_open()) log << line << "\n";
        }

        std::istringstream iss(line);
        std::vector<std::string> a; std::string t;
        while (iss >> t) a.push_back(t);
        if (a.empty()) continue;

        const std::string& cmd = a[0];
        bool reserve_mode = false;
            if (cmd == "exit") break;
        else if (cmd == "help") {
            std::cout << "Built-in: help, uptime, mem, pwd, cd, ls (-a/-s), cat, touch, mkdir, rm, rmdir, edit, gui, install, hexdump, pkglist, remove installed <pkg>, save session, history, clear, exit\n";
        } else if (cmd == "uptime") {
            std::ifstream up("/proc/uptime"); double s = 0;
            if (up >> s) std::cout << "Uptime: " << static_cast<long>(s) << " sec\n";
        } else if (cmd == "mem") {
            if (!check_binos_gate(termux_home)) {
                            std::cout << "sh not executable\n";
                            if (!reserve_mode) {
                                prompt_reserve_mode(termux_home, reserve_mode);
                            }
                            if (reserve_mode && has_reserve_cmd(termux_home, cmd)) {
                                std::cout << "[RESERVE] Executing '" << cmd << "' via .resrevcommands\n";
                            }
                        } else {
                            std::system(("\"" + termux_home + "/myos/bin/binos\" \"" + cfg.bin_dir + "/mem\"").c_str());
                        }
        } else if (cmd == "pwd") std::cout << cwd << "\n";
        else if (cmd == "cd") {
            fs::path dst = (a.size() < 2 || a[1] == "~") ? fs::path(home) : fs::path(expand_tilde(a[1], home));
            fs::current_path(dst, ec);
            if (ec) std::cout << "cd: " << ec.message() << "\n";
        } else if (cmd == "ls") {
            bool show_s = (a.size() >= 2 && a[1] == "-s");
            bool show_a = (a.size() >= 2 && a[1] == "-a");
            std::string raw = (show_s || show_a) ? (a.size() >= 3 ? a[2] : "") : (a.size() >= 2 ? a[1] : "");
            std::string vr = virt_rel(raw, cwd, termux_home);
            if (!vr.empty() && virt_ls(vr)) continue;
            fs::path dst = raw.empty() ? fs::current_path(ec) : fs::path(expand_tilde(raw, home));
            for (const auto& e : fs::directory_iterator(dst, ec)) {
                std::string fn = e.path().filename().string();
                bool is_dot = (!fn.empty() && fn[0] == '.');
                if (!show_a && (show_s ? !is_dot : is_dot)) continue;
                std::cout << fn << (e.is_directory(ec) ? "/  " : "  ");
            }
            std::cout << "\n";
        } else if (cmd == "cat") {
            if (a.size() < 2) { std::cout << "Usage: cat <file>\n"; continue; }
            std::string p = expand_tilde(a[1], home);
            std::string vr = virt_rel(p, cwd, termux_home);
            if (!vr.empty() && virt_cat(vr, cfg, cwd)) continue;
            std::ifstream ifs(p); std::string l;
            if (!ifs.is_open()) { std::cout << "cat: cannot open '" << a[1] << "'\n"; continue; }
            while (std::getline(ifs, l)) std::cout << l << "\n";
        } else if (cmd == "hexdump") {
            if (a.size() < 2) { std::cout << "Usage: hexdump <file>\n"; continue; }
            cmd_hexdump(expand_tilde(a[1], home));
        } else if (cmd == "edit") {
            if (a.size() < 2) { std::cout << "Usage: edit <file>\n"; continue; }
            run_edit(expand_tilde(a[1], home));
        } else if (cmd == "gui") {
            cmd_gui_tui(cfg);
        } else if (cmd == "install") {
            cmd_install_pkg(a, termux_home, cfg);
        } else if (cmd == "pkglist" || (cmd == "pkg" && a.size() >= 2 && a[1] == "list")) {
            std::cout << "Installed packages: ";
            for (size_t i = 0; i < cfg.installed_pkgs.size(); ++i) std::cout << (i ? ", " : "") << cfg.installed_pkgs[i];
            std::cout << "\nRemoved packages  : ";
            for (size_t i = 0; i < cfg.removed_pkgs.size(); ++i) std::cout << (i ? ", " : "") << cfg.removed_pkgs[i];
            std::cout << "\n";
        } else if (cmd == "remove" && a.size() >= 3 && a[1] == "installed") {
            std::string pkg = a[2];
            fs::path bin_p = fs::path(cfg.bin_dir) / pkg;
            if (fs::exists(bin_p, ec)) fs::remove(bin_p, ec);
            cfg.installed_pkgs.erase(std::remove(cfg.installed_pkgs.begin(), cfg.installed_pkgs.end(), pkg), cfg.installed_pkgs.end());
            if (std::find(cfg.removed_pkgs.begin(), cfg.removed_pkgs.end(), pkg) == cfg.removed_pkgs.end()) cfg.removed_pkgs.push_back(pkg);
            cfg.cmds.erase(pkg);
            std::cout << "\033[1;33m[WARN] os.conf will be updated and system will reboot!\033[0m\n";
            save_os_conf(termux_home, cfg);
            std::cout << "Save session to .session.s (Base64) before reboot? [Y/N]: ";
            std::string ans;
            if (std::getline(std::cin, ans) && (ans == "Y" || ans == "y")) {
                write_session_file(session_store_dir(termux_home) + "/.session.s", cwd, last_cmd, true);
                set_restore_once_flag(termux_home, true);
            }
            execl((termux_home + "/myos/bin/reboot").c_str(), "reboot", (char*)nullptr);
            return 0;
        } else if (cmd == "save" && a.size() >= 2 && a[1] == "session") {
            prompt_save_session_cmd(termux_home, cwd, last_cmd);
        } else if (cmd == "touch" && a.size() >= 2) {
            std::ofstream ofs(expand_tilde(a[1], home), std::ios::app);
        } else if (cmd == "mkdir" && a.size() >= 2) {
            fs::create_directories(expand_tilde(a[1], home), ec);
        } else if (cmd == "rm" && a.size() >= 2) {
            fs::remove(expand_tilde(a[1], home), ec);
        } else if (cmd == "rmdir" && a.size() >= 2) {
            if (fs::is_empty(expand_tilde(a[1], home), ec)) fs::remove(expand_tilde(a[1], home), ec);
        } else if (cmd == "clear") {
            std::cout << "\033[2J\033[1;1H";
        } else if (cmd == "history") {
            std::ifstream hs(cfg.history_file); std::string l; int n = 1;
            while (std::getline(hs, l)) std::cout << n++ << ": " << l << "\n";
        } else {
            fs::path bin;
            fs::path py_app = fs::path(cfg.apps_dir) / (cmd + ".py");
            fs::path cs_app = fs::path(cfg.apps_dir) / (cmd + ".cs");
            fs::path mod_bin = fs::path(cfg.sys_root) / "guiapps" / cmd;
            if (fs::exists(mod_bin) && fs::is_regular_file(mod_bin)) {
                std::string run = "\"" + mod_bin.string() + "\"";
                for (size_t i = 1; i < a.size(); ++i) run += " \"" + a[i] + "\"";
                std::system(run.c_str());
            } else if (check_cmd_meta(cfg, cmd, bin)) {
                if (!binos_ready_for_exec(termux_home)) {
                    std::cout << "sh not executable\n";
                    continue;
                }
                std::string run = "\"" + bin.string() + "\"";
                for (size_t i = 1; i < a.size(); ++i) run += " \"" + a[i] + "\"";
                std::system(run.c_str());
            } else if (fs::exists(py_app)) {
                std::system(("python3 \"" + py_app.string() + "\"").c_str());
            } else if (fs::exists(cs_app)) {
                std::string exe = "/data/data/com.termux/files/usr/tmp/" + cmd + ".exe";
                if (std::system(("mcs -out:\"" + exe + "\" \"" + cs_app.string() + "\" >/dev/null 2>&1").c_str()) == 0)
                    std::system(("mono \"" + exe + "\"").c_str());
            } else if (!cfg.cmds.count(cmd)) {
                {
                        fs::path ext_bin = fs::path(cfg.bin_dir) / cmd;
                        std::error_code b_ec;
                        if (!check_binos_gate(termux_home)) {
                            std::cout << "sh not executable\n";
                            if (!reserve_mode) {
                                prompt_reserve_mode(termux_home, reserve_mode);
                            }
                            if (reserve_mode && has_reserve_cmd(termux_home, cmd)) {
                                std::cout << "[RESERVE] Running builtin/reserve command: " << cmd << "\n";
                            }
                        } else if (fs::exists(ext_bin, b_ec) || !cfg.cmds.count(cmd)) {
                            std::string run_cmd = "\"" + termux_home + "/myos/bin/binos\" \"" + ext_bin.string() + "\"";
                            for (size_t i = 1; i < a.size(); ++i) run_cmd += " \"" + a[i] + "\"";
                            int rc = std::system(run_cmd.c_str());
                            if (rc != 0 && !fs::exists(ext_bin, b_ec)) {
                                std::cout << "Unknown command: " << cmd << "\n";
                            }
                        } else {
                            std::cout << "Unknown command: " << cmd << "\n";
                        }
                    }
            }
        }
    }
    return 0;
}
