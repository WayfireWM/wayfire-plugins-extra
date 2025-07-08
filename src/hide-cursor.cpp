/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Scott Moreau
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in the
 * Software without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util.hpp>

namespace wf_hide_cursor
{
bool hidden;

class wayfire_hide_cursor
{
    wf::option_wrapper_t<int> hide_delay{"hide-cursor/hide_delay"};
    wf::wl_timer<false> hide_timer;

  public:
    wayfire_hide_cursor()
    {
        hidden = false;
        setup_hide_timer();
        wf::get_core().connect(&pointer_motion);
    }

    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_motion_event>> pointer_motion =
        [=] (wf::input_event_signal<wlr_pointer_motion_event> *ev)
    {
        setup_hide_timer();
        if (hidden)
        {
            wf::get_core().unhide_cursor();
            hidden = false;
        }
    };

    void setup_hide_timer()
    {
        hide_timer.disconnect();
        hide_timer.set_timeout(hide_delay, [=] ()
        {
            if (!hidden)
            {
                wf::get_core().hide_cursor();
                hidden = true;
            }
        });
    }

    ~wayfire_hide_cursor()
    {
        pointer_motion.disconnect();
        hide_timer.disconnect();
        if (hidden)
        {
            wf::get_core().unhide_cursor();
        }
    }
};

class wayfire_hide_cursor_plugin : public wf::per_output_plugin_instance_t
{
    wf::shared_data::ref_ptr_t<wayfire_hide_cursor> global_idle;

    wf::activator_callback toggle_cb = [=] (auto)
    {
        hidden = !hidden;

        if (hidden)
        {
            wf::get_core().hide_cursor();
        } else
        {
            wf::get_core().unhide_cursor();
        }

        return true;
    };

  public:
    void init() override
    {
        output->add_activator(
            wf::option_wrapper_t<wf::activatorbinding_t>{"hide-cursor/toggle"},
            &toggle_cb);
    }

    void fini() override
    {
        output->rem_binding(&toggle_cb);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_hide_cursor_plugin>);
}
