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

#include <wayfire/config.h>
#if WF_HAS_XWAYLAND
extern "C"
{
    #define class class_t
    #define static
    #include <wlr/xwayland.h>
    #undef static
    #undef class
}
#endif

struct parent_view
{
    nonstd::observer_ptr<wf::view_interface_t> view;
};

static std::map<wf::output_t*, struct parent_view> views;

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

    /* The app-id option is used to identify the application. If app-id
     * is not set or does not match the launched app-id, the plugin will
     * not be able to set the client surface as the background.
     */
    wf::option_wrapper_t<std::string> app_id{"background-view/app-id"};

  public:
    void init() override
    {
        grab_interface->name = transformer_name;
        grab_interface->capabilities = 0;

        command.set_callback(option_changed);
        file.set_callback(option_changed);

        output->connect_signal("view-mapped", &view_mapped);
        output->connect_signal("view-detached", &view_detached);

        option_changed();
    }

    wf::config::option_base_t::updated_callback_t option_changed = [=] ()
    {
        if (views[output].view)
        {
            views[output].view->close();
        }

        if (std::string(command).empty())
        {
            return;
        }

        views[output].view = nullptr;

        wf::get_core().run(std::string(
            command) + add_arg_if_not_empty(file));
    };

    void set_view_for_output(wayfire_view view, wf::output_t *o)
    {
        /* Get rid of decorations */
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
        view->sticky = true;

        /* Set role */
        view->role = wf::VIEW_ROLE_DESKTOP_ENVIRONMENT;

        /* Remember to close it later */
        views[o].view = view;
    }

    wf::signal_connection_t view_detached{[this] (wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);

            if (!view)
            {
                return;
            }

            if (view == views[output].view)
            {
                views[output].view = nullptr;
            }
        }
    };

    wf::signal_connection_t view_mapped{[this] (wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);

            if (!view)
            {
                return;
            }

            for (auto& o : wf::get_core().output_layout->get_outputs())
            {
                if (views[o].view)
                {
                    continue;
                }

                /* Try to match application identifier */
                if (std::string(app_id) == view->get_app_id())
                {
                    set_view_for_output(view, o);
                    break;
                }
            }
        }
    };

    std::string add_arg_if_not_empty(std::string in)
    {
        if (in.empty())
        {
            return in;
        } else
        {
            return " \"" + in + "\"";
        }
    }

    void fini() override
    {
        if (views[output].view)
        {
            views[output].view->close();
        }
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_background_view);
