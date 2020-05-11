/*
 * Copyright © 2020 Till Smejkal
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/output.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/view.hpp>
#include <wayfire/util.hpp>


class wayfire_follow_focus : public wf::plugin_interface_t
{
   private:
    wf::wl_idle_call change_focus;

    wf::option_wrapper_t<bool> should_change_view{"follow-focus/change_view"};
    wf::option_wrapper_t<bool> should_change_output{"follow-focus/change_output"};

    void change_view()
    {
        auto view = wf::get_core().get_cursor_focus_view();
        wf::get_core().set_active_view(view);
    }

    void change_output()
    {
        auto coord = wf::get_core().get_cursor_position();
        for (auto output : wf::get_core().output_layout->get_outputs()) {
            if (output->get_layout_geometry() & coord) {
                wf::get_core().focus_output(output);
                return;
            }
        }
    }

    wf::signal_callback_t pointer_motion = [=] (wf::signal_data_t * /*data*/) {
        change_focus.run_once([=] () {
            if (should_change_view)
                change_view();

            if (should_change_output)
                change_output();
        });
    };

   public:
    void init() override
    {
        grab_interface->name = "follow-focus";
        grab_interface->capabilities = 0;

        wf::get_core().connect_signal("pointer_motion", &pointer_motion);
    }

    void fini() override
    {
        wf::get_core().disconnect_signal("pointer_motion", &pointer_motion);
        change_focus.disconnect();
    }
};


DECLARE_WAYFIRE_PLUGIN(wayfire_follow_focus);
