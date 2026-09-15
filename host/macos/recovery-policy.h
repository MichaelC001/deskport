#pragma once
#include <string>
#include <vector>
namespace DeskPortRecovery {
struct Result { int code; std::string output; };
constexpr const char* Label = "io.github.keithxc.DeskPort";
constexpr const char* Directory = "/Library/Application Support/DeskPort/unattended";
constexpr const char* Program = "/Applications/DeskPort.app/Contents/MacOS/DeskPort";
inline bool hasPid(const std::string& job) {
    std::size_t start = 0;
    while (start < job.size()) {
        const auto end = job.find('\n', start);
        auto line = job.substr(start, end - start);
        const auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos) line.erase(0, first);
        if (line.rfind("pid = ", 0) == 0) {
            const auto value = line.substr(6);
            if (!value.empty() && value[0] >= '1' && value[0] <= '9' &&
                value.find_first_not_of("0123456789") == std::string::npos) return true;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}
inline bool disabled(const std::string& output) {
    return output.find(std::string("\"") + Label + "\" => disabled") != std::string::npos;
}
// Inject only platform operations; the production decision flow is tested without
// touching launchd, root privileges, a real account or a real application.
template<class Run, class File, class Report, class Wait>
int recover(const std::string& home, unsigned uid, Run run, File fileExists, Report report, Wait wait) {
    const std::string directory = home + Directory;
    if (!fileExists(directory + "/enabled") || fileExists(directory + "/paused")) return 0;
    const std::string domain = "gui/" + std::to_string(uid);
    const std::string target = domain + "/" + Label;
    const auto allowed = run({"/bin/launchctl", "print-disabled", domain});
    if (allowed.code != 0) { report(directory, "waiting-session"); return 0; }
    if (disabled(allowed.output)) { report(directory, "disabled"); return 0; }
    auto job = run({"/bin/launchctl", "print", target});
    if (job.code == 0 && hasPid(job.output)) { report(directory, "running"); return 0; }
    const auto external = run({"/usr/bin/pgrep", "-u", std::to_string(uid), "-f",
                              "/DeskPort[^/]*\\.app/Contents/MacOS/DeskPort( |$)"});
    if (external.code == 0) { report(directory, "running"); return 0; }
    if (external.code != 1) { report(directory, "error"); return 1; }
    // Never register a user-created LaunchAgent as root. This code is already
    // permanently unprivileged; launchd starts the GUI process as the user.
    const auto plist = home + "/Library/LaunchAgents/" + Label + ".plist";
    if (!fileExists(plist)) { report(directory, "missing-login-item"); return 0; }
    if (job.code != 0) {
        if (run({"/bin/launchctl", "bootstrap", domain, plist}).code != 0) {
            report(directory, "error"); return 1;
        }
        job = run({"/bin/launchctl", "print", target});
    }
    if (!hasPid(job.output) && run({"/bin/launchctl", "kickstart", target}).code != 0) {
        report(directory, "error"); return 1;
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        job = run({"/bin/launchctl", "print", target});
        if (job.code == 0 && hasPid(job.output)) { report(directory, "running"); return 0; }
        wait();
    }
    report(directory, "error"); return 1;
}
}
