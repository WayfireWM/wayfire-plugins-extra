/*
 * Copyright © 2023 Scott Moreau
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

#include <wayfire/per-output-plugin.hpp>


namespace focus_steal_prevent
{
class wayfire_focus_steal_prevent : public wf::per_output_plugin_instance_t
{
  private:
    wayfire_view focus_view  = nullptr;
    bool prevent_focus_steal = false;
    wf::wl_timer timer;

    wf::option_wrapper_t<int> timeout{"focus-steal-prevent/timeout"};

    wf::signal::connection_t<wf::input_event_signal<wlr_keyboard_key_event>> on_key_event =
        [=] (wf::input_event_signal<wlr_keyboard_key_event> *ev)
    {
        if (!focus_view)
        {
            focus_view = output->get_active_view();
        }

        prevent_focus_steal = true;

        timer.disconnect();
        timer.set_timeout(timeout, [=] ()
        {
            focus_view = nullptr;
            prevent_focus_steal = false;
            return false; // disconnect
        });
    };

    wf::signal::connection_t<wf::focus_view_signal> view_focused = [=] (wf::focus_view_signal *ev)
    {
        if (!prevent_focus_steal)
        {
            return;
        }

        if (ev->view != focus_view)
        {
            output->focus_view(focus_view, true);

            /** Emit the view-hints-changed signal for use with panels */
            wf::view_hints_changed_signal hints_signal;
            hints_signal.view = ev->view;
            hints_signal.demands_attention = true;
            ev->view->emit(&hints_signal);
            wf::get_core().emit(&hints_signal);
        }
    };

  public:
    void init() override
    {
        output->connect(&view_focused);
        wf::get_core().connect(&on_key_event);
    }

    void fini() override
    {
        timer.disconnect();
        view_focused.disconnect();
        on_key_event.disconnect();
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_focus_steal_prevent>);
}
