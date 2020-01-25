/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Scott Moreau
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <map>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include "wayfire/core.hpp"
#include "wayfire/view.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/output.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wayfire/output-layout.hpp"

#include <wayfire/util/log.hpp>

extern "C"
{
#include <wlr/config.h>
#if WLR_HAS_XWAYLAND
#define class class_t
#define static
#include <wlr/xwayland.h>
#undef static
#undef class
#endif
}

struct process
{
    nonstd::observer_ptr<wf::view_interface_t> view;
    struct wl_client *client;
    pid_t pid;
};

static std::map<wf::output_t *, struct process> procs;

class wayfire_background_view : public wf::plugin_interface_t
{
    const std::string transformer_name = "background-view";
    wf::signal_callback_t view_mapped;
    wf::config::option_base_t::updated_callback_t option_changed;
    wf::option_wrapper_t<std::string> command{"background-view/command"};
    wf::option_wrapper_t<std::string> file{"background-view/file"};
    struct wl_event_source *signal = nullptr;

    public:
    void init() override
    {
        grab_interface->name = transformer_name;
        grab_interface->capabilities = 0;

        option_changed = [=] ()
        {
            if (procs[output].view)
            {
                procs[output].view->close();
                kill(procs[output].pid, SIGINT);
                procs[output].view = nullptr;
            }

            procs[output].client = client_launch((std::string(command) + add_arg_if_not_empty(file)).c_str());
        };

        command.set_callback(option_changed);
        file.set_callback(option_changed);

        view_mapped = [=] (wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);
#if WLR_HAS_XWAYLAND
            wlr_surface *wlr_surface = view->get_wlr_surface();
            bool is_xwayland_surface = wlr_surface_is_xwayland_surface(wlr_surface);
#endif

            for (auto& o : wf::get_core().output_layout->get_outputs())
            {
                if (procs[o].client == view->get_client()
#if WLR_HAS_XWAYLAND
                    || (is_xwayland_surface &&
                    /* For this match to work, the client must set _NET_WM_PID */
                    procs[o].pid == wlr_xwayland_surface_from_wlr_surface(wlr_surface)->pid)
#endif
                    )
                {
#if WLR_HAS_XWAYLAND
                    if (is_xwayland_surface)
                        view->set_decoration(nullptr);
#endif

                    /* Move to the respective output */
                    wf::get_core().move_view_to_output(view, o);

                    /* A client should be used that can be resized to any size
                     * If we set it fullscreen, the screensaver would be inhibited
                     * so we send a resize request that is the size of the output
                     */
                    view->set_geometry(o->get_relative_geometry());

                    /* Set it as the background */
                    o->workspace->add_view(view, wf::LAYER_BACKGROUND);

                    /* Make it show on all workspaces */
                    view->role = wf::VIEW_ROLE_DESKTOP_ENVIRONMENT;

                    procs[o].view = view;

                    break;
                }
            }
        };

        output->connect_signal("map-view", &view_mapped);

        if (!signal)
        {
            signal = wl_event_loop_add_signal(wf::get_core().ev_loop, SIGCHLD, sigchld_handler, &procs);
        }

        procs[output].client = client_launch((std::string(command) + add_arg_if_not_empty(file)).c_str());
        procs[output].view = nullptr;
    }

    std::string add_arg_if_not_empty(std::string in)
    {
        if (!in.empty())
        {
            return " \"" + in + "\"";
        }
        else
        {
            return in;
        }
    }

    /* The following functions borrowed from weston */
    int os_fd_set_cloexec(int fd)
    {
        long flags;

        if (fd == -1)
        {
            return -1;
        }

        flags = fcntl(fd, F_GETFD);
        if (flags == -1)
        {
            return -1;
        }

        if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
        {
            return -1;
        }

        return 0;
    }

    int set_cloexec_or_close(int fd)
    {
        if (os_fd_set_cloexec(fd) != 0)
        {
            close(fd);
            return -1;
        }
        return fd;
    }

    int os_socketpair_cloexec(int domain, int type, int protocol, int *sv)
    {
        int ret;

#ifdef SOCK_CLOEXEC
        ret = socketpair(domain, type | SOCK_CLOEXEC, protocol, sv);
        if (ret == 0 || errno != EINVAL)
        {
            return ret;
        }
#endif

        ret = socketpair(domain, type, protocol, sv);
        if (ret < 0)
        {
            return ret;
        }

        sv[0] = set_cloexec_or_close(sv[0]);
        sv[1] = set_cloexec_or_close(sv[1]);

        if (sv[0] != -1 && sv[1] != -1)
        {
            return 0;
        }

        close(sv[0]);
        close(sv[1]);
        return -1;
    }

    static int sigchld_handler(int signal_number, void *data)
    {
        std::map<wf::output_t *, struct process> *procs = (std::map<wf::output_t *, struct process> *) data;
        pid_t pid;
        int status;

        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            for (auto& p : *procs)
            {
                if (p.second.pid == pid)
                {
                    p.second.view = nullptr;
                    break;
                }
            }
        }

        if (pid < 0 && errno != ECHILD)
        {
            LOGE("waitpid error: ", strerror(errno));
        }

        return 1;
    }

    void child_client_exec(int sockfd, const char *command)
    {
        int clientfd;
        char s[32];
        sigset_t allsigs;

        /* do not give our signal mask to the new process */
        sigfillset(&allsigs);
        sigprocmask(SIG_UNBLOCK, &allsigs, NULL);

        /* Launch clients as the user. Do not launch clients with wrong euid. */
        if (seteuid(getuid()) == -1)
        {
            LOGE("seteuid failed");
            return;
        }

        /* SOCK_CLOEXEC closes both ends, so we dup the fd to get a
         * non-CLOEXEC fd to pass through exec. */
        clientfd = dup(sockfd);
        if (clientfd == -1)
        {
            LOGE("dup failed: ", strerror(errno));
            return;
        }

        snprintf(s, sizeof s, "%d", clientfd);
        setenv("WAYLAND_SOCKET", s, 1);
        setenv("WAYLAND_DISPLAY", wf::get_core().wayland_display.c_str(), 1);

        setenv("_JAVA_AWT_WM_NONREPARENTING", "1", 1);

#if WLR_HAS_XWAYLAND
        if (wf::get_core().get_xwayland_display() >= 0)
        {
            auto xdisp = ":" + std::to_string(wf::get_core().get_xwayland_display());
            setenv("DISPLAY", xdisp.c_str(), 1);
        }
#endif

        if (execlp("bash", "bash", "-c", command, NULL) < 0)
        {
            LOGE("executing '", command ,"' failed: ", strerror(errno));
        }
    }

    struct wl_client *client_launch(const char *command)
    {
        int sv[2];
        pid_t pid;
        struct wl_client *client;

        LOGI("launching '", command, "'");

        if (os_socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
        {
            LOGE(__func__, ": socketpair failed while launching '", command , "': ",
                strerror(errno));
            return NULL;
        }

        pid = fork();
        if (pid == -1)
        {
            close(sv[0]);
            close(sv[1]);
            LOGE(__func__, ": fork failed while launching '", command, "': ",
                strerror(errno));
            return NULL;
        }

        if (pid == 0)
        {
            child_client_exec(sv[1], command);
            _exit(-1);
        }

        close(sv[1]);

        client = wl_client_create(wf::get_core().display, sv[0]);
        if (!client)
        {
            close(sv[0]);
            LOGE(__func__, ": wl_client_create failed while launching '", command , "'.");
            return NULL;
        }

        procs[output].pid = pid;

        return client;
    }
    /* End from weston */

    void fini() override
    {
        if (procs[output].view)
        {
            procs[output].view->close();
            kill(procs[output].pid, SIGINT);
        }

        if (signal)
        {
            wl_event_source_remove(signal);
        }

        output->disconnect_signal("map-view", &view_mapped);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_background_view);