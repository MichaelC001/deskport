// Runs briefly in the system domain. Drop all root privileges before reading any
// user-controlled files or asking launchd to start a job in that user's GUI domain.
#include "recovery-policy.h"
#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <limits.h>
using namespace DeskPortRecovery;
static Result run(const std::vector<std::string>& args) {
    int pipefd[2];
    if (pipe(pipefd)) return {-1, {}};
    const pid_t pid = fork();
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO); dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]); close(pipefd[1]);
        std::vector<char*> argv;
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execv(argv[0], argv.data()); _exit(127);
    }
    close(pipefd[1]);
    if (pid < 0) { close(pipefd[0]); return {-1, {}}; }
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    int status = 0;
    for (;;) {
        char buffer[4096]; ssize_t count;
        while ((count = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
            if (output.size() < 65536) output.append(buffer, static_cast<size_t>(count));
        }
        const auto waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            while ((count = read(pipefd[0], buffer, sizeof(buffer))) > 0)
                if (output.size() < 65536) output.append(buffer, static_cast<size_t>(count));
            close(pipefd[0]);
            return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, output};
        }
        if ((waited < 0 && errno != EINTR) || std::chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL); while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            close(pipefd[0]); return {-1, output};
        }
        usleep(20000);
    }
}
static bool fileExists(const std::string& path) {
    struct stat s{};
    return lstat(path.c_str(), &s) == 0 && S_ISREG(s.st_mode) && s.st_uid == getuid();
}
static void report(const std::string& directory, const char* result) {
    const auto temporary = directory + "/heartbeat." + std::to_string(getpid());
    const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return;
    const std::string text = std::string(result) + "\n";
    const bool ok = write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
    close(fd);
    if (ok) rename(temporary.c_str(), (directory + "/heartbeat").c_str());
    unlink(temporary.c_str());
}
int main(int argc, char**) {
    if (argc != 1) return 64;
    char executable[PATH_MAX], resolved[PATH_MAX]; uint32_t size = sizeof(executable);
    if (_NSGetExecutablePath(executable, &size) || !realpath(executable, resolved) ||
        std::string(resolved) != "/Applications/DeskPort.app/Contents/Helpers/deskport-recovery") return 78;
    struct stat console{};
    if (stat("/dev/console", &console) || console.st_uid < 501) return 0;
    struct passwd user{}, *found = nullptr; char storage[16384];
    if (getpwuid_r(console.st_uid, &user, storage, sizeof(storage), &found) || !found) return 1;
    const std::string home = user.pw_dir;
    if (geteuid() == 0) {
        if (initgroups(user.pw_name, user.pw_gid) || setgid(user.pw_gid) || setuid(user.pw_uid)) return 1;
    }
    if (getuid() != console.st_uid || geteuid() != console.st_uid) return 1;
    // No inherited environment can redirect system tools, runtime loading or HOME.
    extern char** environ;
    while (environ && environ[0]) {
        const std::string name = std::string(environ[0]).substr(0, std::string(environ[0]).find('='));
        if (unsetenv(name.c_str())) return 1;
    }
    setenv("HOME", home.c_str(), 1); setenv("PATH", "/usr/bin:/bin:/usr/sbin:/sbin", 1);
    umask(077);
    return recover(home, getuid(), run, fileExists, report, [] { usleep(200000); });
}
