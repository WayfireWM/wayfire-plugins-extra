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

#include <signal.h>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/output-layout.hpp>

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
    pid_t pid;
};

static std::map<wf::output_t *, struct process> procs;

class wayfire_background_view : public wf::plugin_interface_t
{
    const std::string transformer_name = "background-view";
    /* The command option should be set to a client like mpv, projectM or
     * a single xscreensaver.
     */
    wf::option_wrapper_t<std::string> command{"background-view/command"};
    /* The file option is for convenience when using wcm. If file is set,
     * it will be appended to the command, wrapped in double quotes.
     */
    wf::option_wrapper_t<std::string> file{"background-view/file"};

    public:
    void init() override
    {
        grab_interface->name = transformer_name;
        grab_interface->capabilities = 0;

        command.set_callback(option_changed);
        file.set_callback(option_changed);

        output->connect_signal("map-view", &view_mapped);

        option_changed();
    }

    wf::config::option_base_t::updated_callback_t option_changed = [=] ()
    {
        if (procs[output].view)
        {
            procs[output].view->close();
            kill(procs[output].pid, SIGINT);
            procs[output].view = nullptr;
        }
        if (std::string(command).empty())
        {
            return;
        }

        /* Insert exec between environment variables and command.
         * This is needed for shells that fork on -c by default
         * because we are matching the pid and fork increments
         * the pid. We could just check for the pid and pid + 1
         * but this wouldn't work for pid wrap around. For example,
         * if /bin/sh is bash, this is not needed because bash -c
         * execs the command and thus doesn't increment the pid.
         * However if /bin/sh is dash, -c will fork by default and
         * increment the pid unless exec is passed in the command.
         */
        std::stringstream stream(command);
        std::string arg;
        std::string cmd;
        bool arg0_found = false;
        while (std::getline(stream, arg, ' '))
        {
            if (!arg0_found && arg.find('=') == std::string::npos)
            {
                arg0_found = true;
                cmd += "exec ";
            }
            cmd += arg + " ";
        }
        cmd.pop_back();

        procs[output].view = nullptr;
        procs[output].pid = wf::get_core().run(std::string(cmd) + add_arg_if_not_empty(file));
    };

    wf::signal_connection_t view_mapped{[this] (wf::signal_data_t *data)
    {
        auto view = get_signaled_view(data);
#if WLR_HAS_XWAYLAND
        wlr_surface *wlr_surface = view->get_wlr_surface();
        bool is_xwayland_surface = wlr_surface_is_xwayland_surface(wlr_surface);
#endif
        /* Get pid for view */
        pid_t view_pid;
        wl_client_get_credentials(view->get_client(), &view_pid, 0, 0);

        for (auto& o : wf::get_core().output_layout->get_outputs())
        {
            /* This condition attempts to match the pid that we got from run()
             * to the client pid. This will not work in all cases. Naturally,
             * not every command will spawn a view. This wont work well for gtk
             * apps because it has a system where the client will defer to an
             * existing instance with the same app-id and have that instance
             * spawn a new view but have the same pid, meaning when we compare
             * the pids, they wont match unless we happen to be the first to run
             * the app. This does work well with clients such as mpv, projectM
             * and running xscreensavers directly.
             */
            if (procs[o].pid == view_pid
#if WLR_HAS_XWAYLAND
                || (is_xwayland_surface &&
                /* For this match to work, the client must set _NET_WM_PID */
                procs[o].pid == wlr_xwayland_surface_from_wlr_surface(wlr_surface)->pid)
#endif
                )
            {
                view->set_decoration(nullptr);

                /* Move to the respective output */
                wf::get_core().move_view_to_output(view, o, false);

                /* A client should be used that can be resized to any size.
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
    }};

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

    void fini() override
    {
        if (procs[output].view)
        {
            procs[output].view->close();
            kill(procs[output].pid, SIGINT);
        }
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_background_view);