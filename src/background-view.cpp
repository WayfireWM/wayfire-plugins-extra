/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Scott Moreau
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
#include <wayfire/scene-operations.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugins/common/input-grab.hpp>

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

class wayfire_background_view : public wf::per_output_plugin_instance_t,
    public wf::keyboard_interaction_t,
    public wf::pointer_interaction_t,
    public wf::touch_interaction_t
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
    wf::option_wrapper_t<std::string> app_id{"background-view/app_id"};

    /* When this option is true, keyboard, pointer and touch input will
     * be inhibited on the background layer.
     */
    wf::option_wrapper_t<bool> inhibit_input{"background-view/inhibit_input"};

    /* To grab input on the background layer */
    std::unique_ptr<wf::input_grab_t> grab;

  public:
    void init() override
    {
        grab = std::make_unique<wf::input_grab_t>(
            "background-view", output, this, this, this);

        inhibit_input.set_callback(inhibit_input_changed);
        command.set_callback(option_changed);
        file.set_callback(option_changed);

        output->connect(&view_mapped);
        output->connect(&view_detached);

        inhibit_input_changed();
        option_changed();
    }

    void handle_pointer_enter(wf::pointf_t position) override
    {
        wf::get_core().set_cursor("default");
    }

    wf::config::option_base_t::updated_callback_t inhibit_input_changed = [=] ()
    {
        if ((bool)inhibit_input)
        {
            grab->grab_input(wf::scene::layer::BACKGROUND);
        } else
        {
            grab->ungrab_input();
        }
    };

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

    void set_view_for_output(wayfire_toplevel_view view, wf::output_t *o)
    {
        /* Move to the respective output */
        wf::move_view_to_output(view, o, false);

        /* A client should be used that can be resized to any size.
         * If we set it fullscreen, the screensaver would be inhibited
         * so we send a resize request that is the size of the output
         */
        view->set_geometry(o->get_relative_geometry());

        /* Set it as the background */
        view->get_wset()->remove_view(view);
        wf::scene::readd_front(o->node_for_layer(wf::scene::layer::BACKGROUND), view->get_root_node());

        /* Make it show on all workspaces in Expo, Cube, etc. */
        view->role = wf::VIEW_ROLE_DESKTOP_ENVIRONMENT;

        /* Remember to close it later */
        views[o].view = view;
    }

    wf::signal::connection_t<wf::view_disappeared_signal> view_detached{[this] (wf::view_disappeared_signal*
                                                                                ev)
        {
            auto view = ev->view;

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

    wf::signal::connection_t<wf::view_mapped_signal> view_mapped{[this] (wf::view_mapped_signal *ev)
        {
            auto view = wf::toplevel_cast(ev->view);

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
        grab->ungrab_input();
        if (views[output].view)
        {
            views[output].view->close();
        }
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_background_view>);
