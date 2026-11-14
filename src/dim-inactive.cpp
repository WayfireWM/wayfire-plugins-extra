/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Ahmed Abdelhaleem Ahmed
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
 *
 * dim-inactive: dims (and optionally desaturates / fades) every window that
 * does not have the keyboard focus, like Compiz's Trailfocus / ADD Helper.
 * The drawing is done by the obs plugin of this package: this plugin only
 * follows focus changes and calls obs' IPC methods wf/obs/set-view-*
 * internally, so obs must be enabled too. Windows matching the "exclude"
 * condition are never dimmed.
 */

#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/view.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/matcher.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/ipc/ipc-activator.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>
#include <algorithm>
#include <map>

class wayfire_dim_inactive : public wf::plugin_interface_t
{
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc_repo;
    wf::ipc_activator_t toggle{"dim-inactive/toggle"};
    wf::option_wrapper_t<int> brightness_opt{"dim-inactive/brightness"};
    wf::option_wrapper_t<int> saturation_opt{"dim-inactive/saturation"};
    wf::option_wrapper_t<int> opacity_opt{"dim-inactive/opacity"};
    wf::option_wrapper_t<int> duration_opt{"dim-inactive/duration"};
    wf::view_matcher_t exclude{"dim-inactive/exclude"};

    bool enabled = true;
    bool obs_missing_logged = false;
    std::map<uint32_t, bool> dimmed; /* view id -> currently dimmed */

    void call_obs(const std::string& what, wayfire_view view, double value)
    {
        wf::json_t data;
        data["view-id"] = (uint64_t)view->get_id();
        data[what] = value;
        data["duration"] = (uint64_t)std::clamp(int(duration_opt), 0, 5000);
        auto result = ipc_repo->call_method("wf/obs/set-view-" + what, std::move(data));
        if (result.has_member("error") && !obs_missing_logged)
        {
            LOGE("dim-inactive: the obs plugin must be enabled (", what, ")");
            obs_missing_logged = true;
        }
    }

    void apply(wayfire_view view, bool dim)
    {
        call_obs("brightness", view, dim ? std::clamp(int(brightness_opt), 10, 100) / 100.0 : 1.0);
        call_obs("saturation", view, dim ? std::clamp(int(saturation_opt), 0, 100) / 100.0 : 1.0);
        call_obs("opacity", view, dim ? std::clamp(int(opacity_opt), 10, 100) / 100.0 : 1.0);
    }

    /* Dim every mapped toplevel except the focused one; only views whose
     * state changes are touched (force = re-apply to all, after an option
     * change). */
    void update(bool force = false)
    {
        auto active = wf::get_core().seat->get_active_view();
        for (auto& view : wf::get_core().get_all_views())
        {
            if (!view->is_mapped() || !wf::toplevel_cast(view))
            {
                continue;
            }

            bool want = enabled && (view != active) && !exclude.matches(view);
            auto it   = dimmed.find(view->get_id());
            bool have = (it != dimmed.end()) && it->second;
            if ((want != have) || (force && want))
            {
                apply(view, want);
                dimmed[view->get_id()] = want;
            }
        }
    }

    wf::signal::connection_t<wf::keyboard_focus_changed_signal> on_focus =
        [=] (wf::keyboard_focus_changed_signal*) { update(); };

    wf::signal::connection_t<wf::view_mapped_signal> on_mapped =
        [=] (wf::view_mapped_signal*) { update(); };

    wf::signal::connection_t<wf::view_unmapped_signal> on_unmapped =
        [=] (wf::view_unmapped_signal *ev) { dimmed.erase(ev->view->get_id()); };

    wf::ipc_activator_t::handler_t toggle_cb = [=] (wf::output_t*, wayfire_view)
    {
        enabled = !enabled;
        update();
        return true;
    };

  public:
    void init() override
    {
        toggle.set_handler(toggle_cb);
        wf::get_core().connect(&on_focus);
        wf::get_core().connect(&on_mapped);
        wf::get_core().connect(&on_unmapped);
        auto reapply = [=] { update(true); };
        brightness_opt.set_callback(reapply);
        saturation_opt.set_callback(reapply);
        opacity_opt.set_callback(reapply);
        update();
    }

    void fini() override
    {
        /* At shutdown obs may already be unloaded and the views are going
         * away: there is nothing to restore. */
        if (wf::get_core().get_current_state() != wf::compositor_state_t::SHUTDOWN)
        {
            enabled = false;
            update();
        }

        on_focus.disconnect();
        on_mapped.disconnect();
        on_unmapped.disconnect();
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_dim_inactive);
