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
#include <wayfire/view-helpers.hpp>
#include <wayfire/matcher.hpp>
#include <set>
#include <linux/input-event-codes.h>
#include <libevdev/libevdev.h>


namespace focus_steal_prevent
{
class wayfire_focus_steal_prevent : public wf::per_output_plugin_instance_t
{
  private:
    wayfire_view focus_view = nullptr;
    wayfire_view last_focus_view = nullptr;
    bool prevent_focus_steal     = false;
    int modifiers_pressed = 0;
    std::multiset<uint32_t> pressed_keys;
    std::multiset<uint32_t> cancel_keycodes;
    wf::wl_timer<false> timer;

    wf::option_wrapper_t<int> timeout{"focus-steal-prevent/timeout"};
    wf::view_matcher_t deny_focus_views{"focus-steal-prevent/deny_focus_views"};
    wf::option_wrapper_t<std::string> cancel_keys{"focus-steal-prevent/cancel_keys"};

    std::string ltrim(const std::string & s)
    {
        size_t start = s.find_first_not_of(' ');
        return (start == std::string::npos) ? "" : s.substr(start);
    }

    std::string rtrim(const std::string & s)
    {
        size_t end = s.find_last_not_of(' ');
        return (end == std::string::npos) ? "" : s.substr(0, end + 1);
    }

    std::string trim(const std::string & s)
    {
        return rtrim(ltrim(s));
    }

    std::multiset<uint32_t> get_cancel_keycodes(std::string s)
    {
        std::multiset<uint32_t> keycodes;
        /* Split string with | delimiter */
        std::stringstream ss(s);
        std::string name;
        while (!ss.eof())
        {
            getline(ss, name, '|');
            /* Strip whitespace from beginning and end of string */
            name = trim(name);
            /* Check if string is a valid keycode */
            auto keycode = libevdev_event_code_from_name(EV_KEY, name.c_str());
            if (keycode != -1)
            {
                /* Add it to the vector */
                keycodes.insert(keycode);
            }
        }

        return keycodes;
    }

    bool is_cancel_key(uint32_t keycode)
    {
        for (auto k : cancel_keycodes)
        {
            if (k == keycode)
            {
                return true;
            }
        }

        return false;
    }

    void cancel()
    {
        focus_view = nullptr;
        prevent_focus_steal = false;
    }

    void reset_timeout()
    {
        timer.disconnect();
        timer.set_timeout(timeout, [=] ()
        {
            cancel();
        });
    }

    bool is_modifier(uint32_t keycode)
    {
        if ((keycode == KEY_LEFTCTRL) ||
            (keycode == KEY_RIGHTCTRL) ||
            (keycode == KEY_LEFTMETA) ||
            (keycode == KEY_RIGHTMETA) ||
            (keycode == KEY_LEFTALT) ||
            (keycode == KEY_RIGHTALT))
        {
            return true;
        }

        return false;
    }

    wf::signal::connection_t<wf::view_unmapped_signal> on_unmap_event =
        [=] (wf::view_unmapped_signal *ev)
    {
        if (!ev->view)
        {
            return;
        }

        if (ev->view == focus_view)
        {
            focus_view = nullptr;
        }

        if (ev->view == last_focus_view)
        {
            last_focus_view = nullptr;
        }
    };

    wf::signal::connection_t<wf::post_input_event_signal<wlr_keyboard_key_event>> on_key_event =
        [=] (wf::post_input_event_signal<wlr_keyboard_key_event> *ev)
    {
        if (ev->event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
        {
            pressed_keys.insert(ev->event->keycode);
            if (is_modifier(ev->event->keycode))
            {
                modifiers_pressed++;
            }
        }

        if (ev->event->state == WL_KEYBOARD_KEY_STATE_RELEASED)
        {
            if (pressed_keys.count(ev->event->keycode))
            {
                pressed_keys.erase(pressed_keys.find(ev->event->keycode));
            }

            if (is_modifier(ev->event->keycode))
            {
                modifiers_pressed--;
                /* Might happen if modifier is held when plugin is loaded */
                if (modifiers_pressed < 0)
                {
                    modifiers_pressed = 0;
                }
            }

            if (!modifiers_pressed && pressed_keys.empty())
            {
                reset_timeout();
            }

            return;
        }

        if (modifiers_pressed || is_cancel_key(ev->event->keycode))
        {
            timer.disconnect();
            cancel();
            return;
        }

        focus_view = wf::get_active_view_for_output(output);
        prevent_focus_steal = true;
        timer.disconnect();
    };

    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_button_event>> on_button_event =
        [=] (wf::input_event_signal<wlr_pointer_button_event> *ev)
    {
        if ((ev->event->state == WLR_BUTTON_RELEASED) || !prevent_focus_steal)
        {
            return;
        }

        auto view = wf::get_core().get_cursor_focus_view();
        if ((!view || (view && (view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT))) &&
            (ev->event->state == WLR_BUTTON_PRESSED) && prevent_focus_steal)
        {
            cancel();
            return;
        }

        focus_view = wf::get_core().get_cursor_focus_view();
        reset_timeout();
    };

    void validate_last_focus_view()
    {
        for (auto & view : wf::get_core().get_all_views())
        {
            if (view == last_focus_view)
            {
                return;
            }
        }

        last_focus_view = nullptr;
    }

    wf::signal::connection_t<wf::view_focus_request_signal> pre_view_focused =
        [=] (wf::view_focus_request_signal *ev)
    {
        validate_last_focus_view();

        if (ev->view && deny_focus_views.matches(ev->view))
        {
            ev->carried_out = true;
            if (last_focus_view)
            {
                wf::view_bring_to_front(last_focus_view);
            }
        }

        last_focus_view = ev->view;

        if (!prevent_focus_steal)
        {
            return;
        }

        if (ev->view != focus_view)
        {
            pre_view_focused.disconnect();

            if (focus_view)
            {
                ev->carried_out = true;
                wf::view_bring_to_front(focus_view);
            }

            if (ev->view)
            {
                /** Emit the view-hints-changed signal for use with panels */
                wf::view_hints_changed_signal hints_signal;
                hints_signal.view = ev->view;
                hints_signal.demands_attention = true;
                ev->view->emit(&hints_signal);
                wf::get_core().emit(&hints_signal);
            }

            wf::get_core().connect(&pre_view_focused);
        }
    };

    wf::config::option_base_t::updated_callback_t cancel_keys_changed = [=] ()
    {
        cancel_keycodes = get_cancel_keycodes(cancel_keys);
    };

  public:
    void init() override
    {
        cancel_keys.set_callback(cancel_keys_changed);
        wf::get_core().connect(&pre_view_focused);
        wf::get_core().connect(&on_button_event);
        wf::get_core().connect(&on_unmap_event);
        wf::get_core().connect(&on_key_event);
        cancel_keys_changed();
    }

    void fini() override
    {
        timer.disconnect();
        on_key_event.disconnect();
        on_unmap_event.disconnect();
        on_button_event.disconnect();
        pre_view_focused.disconnect();
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_focus_steal_prevent>);
}
