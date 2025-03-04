/*
 * Copyright (c) 2019-2020, Sergey Bugaev <bugaevc@serenityos.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Service.h"
#include <AK/HashMap.h>
#include <AK/JsonArray.h>
#include <AK/JsonObject.h>
#include <LibCore/ConfigFile.h>
#include <LibCore/File.h>
#include <LibCore/Socket.h>
#include <grp.h>
#include <libgen.h>
#include <pwd.h>
#include <sched.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static HashMap<pid_t, Service*> s_service_map;

Service* Service::find_by_pid(pid_t pid)
{
    auto it = s_service_map.find(pid);
    if (it == s_service_map.end())
        return nullptr;
    return (*it).value;
}

void Service::setup_socket()
{
    ASSERT(!m_socket_path.is_null());
    ASSERT(m_socket_fd == -1);

    auto ok = Core::File::ensure_parent_directories(m_socket_path);
    ASSERT(ok);

    // Note: we use SOCK_CLOEXEC here to make sure we don't leak every socket to
    // all the clients. We'll make the one we do need to pass down !CLOEXEC later
    // after forking off the process.
    m_socket_fd = socket(AF_LOCAL, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_socket_fd < 0) {
        perror("socket");
        ASSERT_NOT_REACHED();
    }

    if (m_account.has_value()) {
        auto& account = m_account.value();
        if (fchown(m_socket_fd, account.uid(), account.gid()) < 0) {
            perror("fchown");
            ASSERT_NOT_REACHED();
        }
    }

    if (fchmod(m_socket_fd, m_socket_permissions) < 0) {
        perror("fchmod");
        ASSERT_NOT_REACHED();
    }

    auto socket_address = Core::SocketAddress::local(m_socket_path);
    auto un_optional = socket_address.to_sockaddr_un();
    if (!un_optional.has_value()) {
        dbg() << "Socket name " << m_socket_path << " is too long. BUG! This should have failed earlier!";
        ASSERT_NOT_REACHED();
    }
    auto un = un_optional.value();
    int rc = bind(m_socket_fd, (const sockaddr*)&un, sizeof(un));
    if (rc < 0) {
        perror("bind");
        ASSERT_NOT_REACHED();
    }

    rc = listen(m_socket_fd, 16);
    if (rc < 0) {
        perror("listen");
        ASSERT_NOT_REACHED();
    }
}

void Service::setup_notifier()
{
    ASSERT(m_lazy);
    ASSERT(m_socket_fd >= 0);
    ASSERT(!m_socket_notifier);

    m_socket_notifier = Core::Notifier::construct(m_socket_fd, Core::Notifier::Event::Read, this);
    m_socket_notifier->on_ready_to_read = [this] {
        handle_socket_connection();
    };
}

void Service::handle_socket_connection()
{
#ifdef SERVICE_DEBUG
    dbg() << "Ready to read on behalf of " << name();
#endif
    if (m_accept_socket_connections) {
        int accepted_fd = accept(m_socket_fd, nullptr, nullptr);
        if (accepted_fd < 0) {
            perror("accept");
            return;
        }
        spawn(accepted_fd);
        close(accepted_fd);
    } else {
        remove_child(*m_socket_notifier);
        m_socket_notifier = nullptr;
        spawn(m_socket_fd);
    }
}

void Service::activate()
{
    ASSERT(m_pid < 0);

    if (m_lazy)
        setup_notifier();
    else
        spawn(m_socket_fd);
}

void Service::spawn(int socket_fd)
{
#ifdef SERVICE_DEBUG
    dbg() << "Spawning " << name();
#endif

    m_run_timer.start();
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        dbg() << "Failed to spawn " << name() << ". Sucks, dude :(";
    } else if (pid == 0) {
        // We are the child.

        if (!m_working_directory.is_null()) {
            if (chdir(m_working_directory.characters()) < 0) {
                perror("chdir");
                ASSERT_NOT_REACHED();
            }
        }

        struct sched_param p;
        p.sched_priority = m_priority;
        int rc = sched_setparam(0, &p);
        if (rc < 0) {
            perror("sched_setparam");
            ASSERT_NOT_REACHED();
        }

        if (!m_stdio_file_path.is_null()) {
            close(STDIN_FILENO);
            int fd = open(m_stdio_file_path.characters(), O_RDWR, 0);
            ASSERT(fd <= 0);
            if (fd < 0) {
                perror("open");
                ASSERT_NOT_REACHED();
            }
            dup2(STDIN_FILENO, STDOUT_FILENO);
            dup2(STDIN_FILENO, STDERR_FILENO);

            if (isatty(STDIN_FILENO)) {
                ioctl(STDIN_FILENO, TIOCSCTTY);
            }
        } else {
            if (isatty(STDIN_FILENO)) {
                ioctl(STDIN_FILENO, TIOCNOTTY);
            }
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);

            int fd = open("/dev/null", O_RDWR);
            ASSERT(fd == STDIN_FILENO);
            dup2(STDIN_FILENO, STDOUT_FILENO);
            dup2(STDIN_FILENO, STDERR_FILENO);
        }

        if (socket_fd >= 0) {
            ASSERT(!m_socket_path.is_null());
            ASSERT(socket_fd > 3);
            dup2(socket_fd, 3);
            // The new descriptor is !CLOEXEC here.
            setenv("SOCKET_TAKEOVER", "1", true);
        }

        if (m_account.has_value()) {
            auto& account = m_account.value();
            if (setgid(account.gid()) < 0 || setgroups(account.extra_gids().size(), account.extra_gids().data()) < 0 || setuid(account.uid()) < 0) {
                dbgln("Failed to drop privileges (GID={}, UID={})\n", account.gid(), account.uid());
                exit(1);
            }
            setenv("HOME", account.home_directory().characters(), true);
        }

        for (String& env : m_environment)
            putenv(const_cast<char*>(env.characters()));

        char* argv[m_extra_arguments.size() + 2];
        argv[0] = const_cast<char*>(m_executable_path.characters());
        for (size_t i = 0; i < m_extra_arguments.size(); i++)
            argv[i + 1] = const_cast<char*>(m_extra_arguments[i].characters());
        argv[m_extra_arguments.size() + 1] = nullptr;

        rc = execv(argv[0], argv);
        perror("exec");
        ASSERT_NOT_REACHED();
    } else if (!m_multi_instance) {
        // We are the parent.
        m_pid = pid;
        s_service_map.set(pid, this);
    }
}

void Service::did_exit(int exit_code)
{
    ASSERT(m_pid > 0);
    ASSERT(!m_multi_instance);

    dbg() << "Service " << name() << " has exited with exit code " << exit_code;

    s_service_map.remove(m_pid);
    m_pid = -1;

    if (!m_keep_alive)
        return;

    int run_time_in_msec = m_run_timer.elapsed();
    bool exited_successfully = exit_code == 0;

    if (!exited_successfully && run_time_in_msec < 1000) {
        switch (m_restart_attempts) {
        case 0:
            dbgln("Trying again");
            break;
        case 1:
            dbgln("Third time's a charm?");
            break;
        default:
            dbg() << "Giving up on " << name() << ". Good luck!";
            return;
        }
        m_restart_attempts++;
    }

    activate();
}

Service::Service(const Core::ConfigFile& config, const StringView& name)
    : Core::Object(nullptr)
{
    ASSERT(config.has_group(name));

    set_name(name);
    m_executable_path = config.read_entry(name, "Executable", String::formatted("/bin/{}", this->name()));
    m_extra_arguments = config.read_entry(name, "Arguments", "").split(' ');
    m_stdio_file_path = config.read_entry(name, "StdIO");

    String prio = config.read_entry(name, "Priority");
    if (prio == "low")
        m_priority = 10;
    else if (prio == "normal" || prio.is_null())
        m_priority = 30;
    else if (prio == "high")
        m_priority = 50;
    else
        ASSERT_NOT_REACHED();

    m_keep_alive = config.read_bool_entry(name, "KeepAlive");
    m_lazy = config.read_bool_entry(name, "Lazy");

    m_user = config.read_entry(name, "User");
    if (!m_user.is_null()) {
        auto result = Core::Account::from_name(m_user.characters());
        if (result.is_error())
            warnln("Failed to resolve user {}: {}", m_user, result.error());
        else
            m_account = result.value();
    }

    m_working_directory = config.read_entry(name, "WorkingDirectory");
    m_environment = config.read_entry(name, "Environment").split(' ');
    m_boot_modes = config.read_entry(name, "BootModes", "graphical").split(',');
    m_multi_instance = config.read_bool_entry(name, "MultiInstance");
    m_accept_socket_connections = config.read_bool_entry(name, "AcceptSocketConnections");

    m_socket_path = config.read_entry(name, "Socket");

    // Lazy requires Socket.
    ASSERT(!m_lazy || !m_socket_path.is_null());
    // AcceptSocketConnections always requires Socket, Lazy, and MultiInstance.
    ASSERT(!m_accept_socket_connections || (!m_socket_path.is_null() && m_lazy && m_multi_instance));
    // MultiInstance doesn't work with KeepAlive.
    ASSERT(!m_multi_instance || !m_keep_alive);
    // Socket path (plus NUL) must fit into the structs sent to the Kernel.
    ASSERT(m_socket_path.length() < UNIX_PATH_MAX);

    if (!m_socket_path.is_null() && is_enabled()) {
        auto socket_permissions_string = config.read_entry(name, "SocketPermissions", "0600");
        m_socket_permissions = strtol(socket_permissions_string.characters(), nullptr, 8) & 04777;
        setup_socket();
    }
}

void Service::save_to(JsonObject& json)
{
    Core::Object::save_to(json);

    json.set("executable_path", m_executable_path);

    // FIXME: This crashes Inspector.
    /*
    JsonArray extra_args;
    for (String& arg : m_extra_arguments)
        extra_args.append(arg);
    json.set("extra_arguments", move(extra_args));

    JsonArray boot_modes;
    for (String& mode : m_boot_modes)
        boot_modes.append(mode);
    json.set("boot_modes", boot_modes);

    JsonArray environment;
    for (String& env : m_environment)
        boot_modes.append(env);
    json.set("environment", environment);
    */

    json.set("stdio_file_path", m_stdio_file_path);
    json.set("priority", m_priority);
    json.set("keep_alive", m_keep_alive);
    json.set("socket_path", m_socket_path);
    json.set("socket_permissions", m_socket_permissions);
    json.set("lazy", m_lazy);
    json.set("user", m_user);
    json.set("multi_instance", m_multi_instance);
    json.set("accept_socket_connections", m_accept_socket_connections);

    if (m_pid > 0)
        json.set("pid", m_pid);
    else
        json.set("pid", nullptr);

    json.set("restart_attempts", m_restart_attempts);
    json.set("working_directory", m_working_directory);
}

bool Service::is_enabled() const
{
    extern String g_boot_mode;
    return m_boot_modes.contains_slow(g_boot_mode);
}
