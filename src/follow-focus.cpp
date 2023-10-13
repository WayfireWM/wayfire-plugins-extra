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
#include <wayfire/geometry.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/output.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/view.hpp>
#include <wayfire/util.hpp>
#include <wayfire/seat.hpp>

namespace follow_focus
{
/* Since plugin instances are per-output, we need to make this global */
static wf::output_t *focus_output;

class wayfire_follow_focus : public wf::per_output_plugin_instance_t
{
  private:
    wayfire_view focus_view = nullptr;
    wf::wl_timer<false> change_output_focus, change_view_focus;
    wf::point_t last_output_coords, last_view_coords;

    wf::option_wrapper_t<bool> should_change_view{"follow-focus/change_view"};
    wf::option_wrapper_t<bool> should_change_output{"follow-focus/change_output"};
    wf::option_wrapper_t<int> focus_delay{"follow-focus/focus_delay"};
    wf::option_wrapper_t<int> threshold{"follow-focus/threshold"};
    wf::option_wrapper_t<bool> raise_on_top{"follow-focus/raise_on_top"};

    void change_view()
    {
        auto view = wf::get_core().get_cursor_focus_view();
        if (view)
        {
            wf::get_core().seat->focus_view(view);
            if (raise_on_top)
            {
                view_bring_to_front(view);
            }
        }
    }

    void change_output()
    {
        auto cpf = wf::get_core().get_cursor_position();
        wf::point_t coords{static_cast<int>(cpf.x), static_cast<int>(cpf.y)};

        if (output->get_layout_geometry() & coords && (output == focus_output))
        {
            wf::get_core().seat->focus_output(output);
        }
    }

    void check_output()
    {
        change_output_focus.disconnect();

        if (!should_change_output)
        {
            return;
        }

        if (output == wf::get_core().seat->get_active_output())
        {
            return;
        }

        auto cpf = wf::get_core().get_cursor_position();
        wf::point_t coords{static_cast<int>(cpf.x), static_cast<int>(cpf.y)};

        if (output->get_layout_geometry() & coords && (output != focus_output))
        {
            last_output_coords = coords;
            focus_output = output;
        }

        if (abs(coords - last_output_coords) < threshold)
        {
            return;
        }

        if (!focus_delay)
        {
            change_output();

            return;
        }

        change_output_focus.set_timeout(focus_delay, [=] ()
        {
            change_output();
            return false; // disconnect
        });
    }

    void check_view()
    {
        change_view_focus.disconnect();

        if (!should_change_view)
        {
            return;
        }

        auto view = wf::get_core().get_cursor_focus_view();

        if (view == wf::get_active_view_for_output(output))
        {
            focus_view = view;

            return;
        }

        if (!view || (view->role != wf::VIEW_ROLE_TOPLEVEL) ||
            (wf::get_view_layer(view) != wf::scene::layer::WORKSPACE))
        {
            return;
        }

        auto cpf = wf::get_core().get_cursor_position();
        wf::point_t coords{static_cast<int>(cpf.x), static_cast<int>(cpf.y)};
        if (view != focus_view)
        {
            last_view_coords = coords;
            focus_view = view;
        }

        if (abs(coords - last_view_coords) < threshold)
        {
            return;
        }

        if (!focus_delay)
        {
            change_view();

            return;
        }

        change_view_focus.set_timeout(focus_delay, [=] ()
        {
            change_view();
            return false; // disconnect
        });
    }

    wf::signal::connection_t<wf::post_input_event_signal<wlr_pointer_motion_event>> pointer_motion =
        [=] (wf::post_input_event_signal<wlr_pointer_motion_event> *ev)
    {
        check_output();
        check_view();
    };

    wf::signal::connection_t<wf::post_input_event_signal<wlr_pointer_motion_absolute_event>>
    pointer_motion_absolute =
        [=] (wf::post_input_event_signal<wlr_pointer_motion_absolute_event> *ev)
    {
        check_output();
        check_view();
    };

  public:
    void init() override
    {
        wf::get_core().connect(&pointer_motion);
        wf::get_core().connect(&pointer_motion_absolute);
    }

    void fini() override
    {
        change_output_focus.disconnect();
        change_view_focus.disconnect();
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_follow_focus>);
}
