/*
 * Copyright (c) 2026, the Halcyon Setup contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Command.h"
#include <AK/Vector.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

namespace Halcyon {

static ByteString bytes_to_byte_string(Vector<u8> const& bytes)
{
    if (bytes.is_empty())
        return {};
    return ByteString { StringView { reinterpret_cast<char const*>(bytes.data()), bytes.size() } };
}

ByteString resolve_command(StringView program)
{
    if (program.is_empty())
        return {};

    if (program.contains('/')) {
        if (access(ByteString { program }.characters(), X_OK) == 0)
            return ByteString { program };
        return {};
    }

    auto const* path = getenv("PATH");
    // Keep the ByteString alive: split() returns views into it.
    auto path_string = path && *path ? ByteString { path } : ByteString {};
    auto directories = path_string.split(':');
    for (auto const& directory : directories) {
        auto candidate = ByteString::formatted("{}/{}", directory.is_empty() ? "."sv : directory.view(), program);
        if (access(candidate.characters(), X_OK) == 0)
            return candidate;
    }

    // pkexec and some desktop launchers hand the installer a shortened PATH.
    // Fall back to the standard system directories before giving up.
    auto fallback_path = ByteString { "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"sv };
    for (auto const& directory : fallback_path.split(':')) {
        auto candidate = ByteString::formatted("{}/{}", directory, program);
        if (access(candidate.characters(), X_OK) == 0)
            return candidate;
    }
    return {};
}

bool command_exists(StringView program)
{
    return !resolve_command(program).is_empty();
}

CommandResult run_command(ByteString const& program, Vector<ByteString> const& arguments, ByteString const& input, int timeout_seconds)
{
    CommandResult result;

    auto resolved = resolve_command(program);
    if (resolved.is_empty()) {
        result.exit_code = 127;
        result.error = ByteString::formatted("{}: command not found", program);
        return result;
    }

    Vector<char const*> argv;
    argv.append(resolved.characters());
    for (auto const& argument : arguments)
        argv.append(argument.characters());
    argv.append(nullptr);

    // Force a stable C locale for tools whose output we parse.
    Vector<char*> environment;
    for (auto** entry = environ; entry && *entry; ++entry) {
        if (strncmp(*entry, "LC_ALL=", 7) == 0 || strncmp(*entry, "LANG=", 5) == 0 || strncmp(*entry, "LC_MESSAGES=", 12) == 0)
            continue;
        environment.append(*entry);
    }
    environment.append(const_cast<char*>("LC_ALL=C"));
    environment.append(nullptr);

    int stdin_pipe[2] {};
    int stdout_pipe[2] {};
    int stderr_pipe[2] {};
    if (pipe2(stdin_pipe, O_CLOEXEC) != 0 || pipe2(stdout_pipe, O_CLOEXEC) != 0 || pipe2(stderr_pipe, O_CLOEXEC) != 0) {
        result.exit_code = -1;
        result.error = ByteString::formatted("Could not create pipes for {}: {}", program, strerror(errno));
        return result;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, stdin_pipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stdout_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, stderr_pipe[1], STDERR_FILENO);

    pid_t child = -1;
    auto spawn_result = posix_spawnp(&child, resolved.characters(), &actions, nullptr, const_cast<char**>(argv.data()), environment.data());
    posix_spawn_file_actions_destroy(&actions);

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    if (spawn_result != 0) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        result.exit_code = 127;
        result.error = ByteString::formatted("Could not run {}: {}", program, strerror(spawn_result));
        return result;
    }

    if (input.is_empty()) {
        close(stdin_pipe[1]);
    } else {
        auto remaining = input.view();
        while (!remaining.is_empty()) {
            auto written = write(stdin_pipe[1], remaining.characters_without_null_termination(), remaining.length());
            if (written < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            remaining = remaining.substring_view(static_cast<size_t>(written));
        }
        close(stdin_pipe[1]);
    }

    Vector<u8> output;
    Vector<u8> error;
    struct pollfd poll_fds[2] {
        { stdout_pipe[0], POLLIN, 0 },
        { stderr_pipe[0], POLLIN, 0 },
    };
    int open_descriptors = 2;
    bool child_exited = false;
    int status = 0;
    auto start_time = time(nullptr);

    while (open_descriptors > 0 || !child_exited) {
        if (!child_exited) {
            auto waited = waitpid(child, &status, WNOHANG);
            if (waited == child) {
                child_exited = true;
            } else if (waited < 0 && errno != EINTR) {
                child_exited = true;
            }
        }

        if (!child_exited && timeout_seconds > 0 && time(nullptr) - start_time >= timeout_seconds) {
            kill(child, SIGTERM);
            kill(child, SIGKILL);
            result.timed_out = true;
        }

        if (open_descriptors <= 0) {
            if (child_exited)
                break;
            // Wait for the child without busy-looping.
            usleep(5000);
            continue;
        }

        auto poll_result = poll(poll_fds, 2, 100);
        if (poll_result < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        for (auto& descriptor : poll_fds) {
            if (descriptor.fd < 0)
                continue;
            if (!(descriptor.revents & (POLLIN | POLLHUP | POLLERR)))
                continue;

            char buffer[4096];
            auto bytes_read = read(descriptor.fd, buffer, sizeof(buffer));
            if (bytes_read > 0) {
                auto& destination = descriptor.fd == stdout_pipe[0] ? output : error;
                for (ssize_t i = 0; i < bytes_read; ++i)
                    destination.append(static_cast<u8>(buffer[i]));
            } else if (bytes_read == 0 || (errno != EINTR && errno != EAGAIN)) {
                close(descriptor.fd);
                descriptor.fd = -1;
                --open_descriptors;
            }
        }
    }

    if (poll_fds[0].fd >= 0)
        close(poll_fds[0].fd);
    if (poll_fds[1].fd >= 0)
        close(poll_fds[1].fd);

    if (!child_exited) {
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
    }

    if (result.timed_out) {
        result.exit_code = 124;
        result.error = ByteString::formatted("Command timed out after {} seconds.", timeout_seconds);
    } else if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    } else {
        result.exit_code = -1;
    }

    result.output = bytes_to_byte_string(output);
    result.error = bytes_to_byte_string(error);
    return result;
}

}
