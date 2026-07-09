#include "CommandExecutor.h"

#include <unistd.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <algorithm>
#include <cstring>
#include <cerrno>
#include <vector>

namespace containercp::runtime {

CommandResult CommandExecutor::run(const std::vector<std::string>& args,
                                   const std::string& workdir) const {
    CommandResult result;

    if (args.empty()) {
        result.err = "No arguments provided";
        return result;
    }

    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};

    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        result.err = "pipe() failed: " + std::string(strerror(errno));
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        result.err = "fork() failed: " + std::string(strerror(errno));
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return result;
    }

    if (pid == 0) {
        close(stdout_pipe[0]); close(stderr_pipe[0]);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]); close(stderr_pipe[1]);
        if (!workdir.empty()) chdir(workdir.c_str());
        std::vector<char*> argv;
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    // Read both pipes concurrently via poll() to prevent deadlock
    // when both stdout and stderr fill their pipe buffers.
    char buf[4096];
    bool out_done = false, err_done = false;

    while (!out_done || !err_done) {
        struct pollfd fds[2];
        int nfds = 0;
        int out_idx = -1, err_idx = -1;

        if (!out_done) {
            out_idx = nfds;
            fds[nfds].fd = stdout_pipe[0];
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            ++nfds;
        }
        if (!err_done) {
            err_idx = nfds;
            fds[nfds].fd = stderr_pipe[0];
            fds[nfds].events = POLLIN;
            fds[nfds].revents = 0;
            ++nfds;
        }

        int ret = poll(fds, nfds, -1);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (out_idx >= 0 && (fds[out_idx].revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t n = read(stdout_pipe[0], buf, sizeof(buf) - 1);
            if (n > 0) { buf[n] = '\0'; result.out += buf; }
            else out_done = true;
        }
        if (err_idx >= 0 && (fds[err_idx].revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t n = read(stderr_pipe[0], buf, sizeof(buf) - 1);
            if (n > 0) { buf[n] = '\0'; result.err += buf; }
            else { err_done = true; }
        }
    }

    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    }

    return result;
}

} // namespace containercp::runtime
