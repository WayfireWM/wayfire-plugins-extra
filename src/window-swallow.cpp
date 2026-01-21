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
#include <wayfire/workspace-manager.hpp>
#include <wayfire/matcher.hpp>


namespace wayfire_window_swallow
{
std::map<wayfire_view, std::vector<std::pair<wayfire_view, wf::geometry_t>>> swallowed_views;
std::map<wayfire_view, wf::geometry_t> swallowed_geometries;
wayfire_view last_focus_view    = nullptr;
wayfire_view current_focus_view = nullptr;

/* Hack: When we swallow a view, we want the size to match the swallower size,
 * so we call set_seometry() to set it. However, the geometry might be changed
 * if the swallowed view has server side decorations, because adding decoration
 * triggers a set_geometry() call as well. However it sets it to the size of
 * the current geometry and not the geometry of what we intended. So to work
 * around this problem, we set a bool to true on mapped and set it to false
 * a short time later. Then if the geometry was changed right after we set it,
 * change it back to the geometry we intended. */
wf::wl_idle_call idle_set_geometry;
wf::wl_timer no_longer_newly_mapped;
bool newly_mapped = false;

wf::view_matcher_t swallower_views{"window-swallow/swallower_views"};

class window_swallow : public wf::per_output_plugin_instance_t
{
  private:

    wf::signal::connection_t<wf::focus_view_signal> view_focused = [=] (wf::focus_view_signal *ev)
    {
        if (!ev->view)
        {
            return;
        }

        if (swallower_views.matches(ev->view))
        {
            if (current_focus_view && swallower_views.matches(current_focus_view))
            {
                last_focus_view = current_focus_view;
            }

            current_focus_view = ev->view;
        }
    };

    void hide_view(wayfire_view hiding, wayfire_view swallowed)
    {
        if (!hiding || !swallowed)
        {
            return;
        }

        output->workspace->remove_view(hiding);
        std::vector<std::pair<wayfire_view, wf::geometry_t>> v;
        try {
            v = swallowed_views.at(hiding);
        } catch (const std::out_of_range&)
        {}

        std::pair<wayfire_view, wf::geometry_t> p(hiding, hiding->get_wm_geometry());
        v.push_back(p);
        swallowed_views[swallowed] = v;
        swallowed->connect(&view_geometry_changed);
        hiding->set_output(nullptr);
    }

    wf::signal::connection_t<wf::view_geometry_changed_signal> view_geometry_changed{[this] (wf::
                                                                                             view_geometry_changed_signal
                                                                                             *ev)
        {
            auto view = ev->view;
            wf::geometry_t g;
            try {
                g = swallowed_geometries.at(view);
            } catch (const std::out_of_range&)
            {
                return;
            }

            if (newly_mapped)
            {
                idle_set_geometry.run_once([=] ()
                {
                    view->disconnect(&view_geometry_changed);
                    view->set_geometry(g);
                    view->connect(&view_geometry_changed);
                });
            } else
            {
                swallowed_geometries[view] = view->get_wm_geometry();
            }
        }
    };

    wf::signal::connection_t<wf::view_mapped_signal> view_mapped{[this] (wf::view_mapped_signal *ev)
        {
            auto view = ev->view;

            if (last_focus_view)
            {
                current_focus_view = last_focus_view;
                last_focus_view    = nullptr;
            }

            if ((view == current_focus_view) || !view || (view->role != wf::VIEW_ROLE_TOPLEVEL) ||
                !swallower_views.matches(current_focus_view))
            {
                return;
            }

            /* swallow */
            wf::geometry_t g;
            if (current_focus_view->get_decoration())
            {
                auto g1 = current_focus_view->get_wm_geometry();
                auto g2 = current_focus_view->get_decoration()->expand_wm_geometry(
                    current_focus_view->get_wm_geometry());

                g = {g2.x - (g2.x - g1.x), g2.y - (g2.y - g1.y), g1.width, g1.height};
            } else
            {
                g = current_focus_view->get_wm_geometry();
            }

            view->set_geometry(g);
            swallowed_geometries[view] = g;
            ev->is_positioned = true;
            hide_view(current_focus_view, view);
            current_focus_view = view;
            newly_mapped = true;
            no_longer_newly_mapped.disconnect();
            no_longer_newly_mapped.set_timeout(250, [=] ()
            {
                newly_mapped = false;
                return false; // disconnect
            });
        }
    };

    wayfire_view unhide_view(std::vector<std::pair<wayfire_view, wf::geometry_t>> & v, wayfire_view swallowed)
    {
        auto p = v.back();
        auto unhiding = p.first;
        unhiding->set_output(output);
        wf::get_core().move_view_to_output(unhiding, output, true);
        output->workspace->add_view(unhiding, wf::LAYER_WORKSPACE);
        auto g = p.second;
        auto saved_geometry = g;
        try {
            saved_geometry = swallowed_geometries.at(swallowed);
            swallowed_geometries.erase(swallowed_geometries.find(swallowed));
        } catch (const std::out_of_range&)
        {}

        g.x = saved_geometry.x;
        g.y = saved_geometry.y;
        if (g.width != saved_geometry.width)
        {
            g.x += (saved_geometry.width - g.width) / 2;
        }

        if (g.height != saved_geometry.height)
        {
            g.y += (saved_geometry.height - g.height) / 2;
        }

        unhiding->set_geometry(g);
        current_focus_view = unhiding;
        v.erase(std::remove(v.begin(), v.end(), p), v.end());
        if (!v.empty())
        {
            swallowed_views[unhiding] = v;
        }

        return unhiding;
    }

    void prune()
    {
        auto all_views = wf::get_core().get_all_views();
        for (auto & [swallowed, v] : swallowed_views)
        {
            for (auto & p : v)
            {
                if (std::find(all_views.begin(), all_views.end(), p.first) == all_views.end())
                {
                    /* Prune view in swallowed_views but not in all_views */
                    v.erase(std::remove(v.begin(), v.end(), p), v.end());
                }
            }
        }
    }

    wf::signal::connection_t<wf::view_unmapped_signal> view_unmapped{[this] (wf::view_unmapped_signal *ev)
        {
            auto view = ev->view;

            if (view == current_focus_view)
            {
                current_focus_view = nullptr;
            }

            if (view == last_focus_view)
            {
                last_focus_view = nullptr;
            }

            if (!view)
            {
                return;
            }

            prune();

            std::vector<std::pair<wayfire_view, wf::geometry_t>> v;
            try {
                v = swallowed_views.at(view);
            } catch (const std::out_of_range&)
            {
                return;
            }

            if (v.empty())
            {
                swallowed_views.erase(swallowed_views.find(view));
                return;
            }

            /* unswallow */
            unhide_view(v, view);
            swallowed_views.erase(swallowed_views.find(view));
        }
    };

  public:
    void init() override
    {
        output->connect(&view_mapped);
        output->connect(&view_focused);
        output->connect(&view_unmapped);
    }

    void fini() override
    {
        for (auto & [s, hidden_views] : swallowed_views)
        {
            auto swallowed = s;
            while (!hidden_views.empty())
            {
                swallowed = unhide_view(hidden_views, swallowed);
            }
        }

        swallowed_views.clear();

        view_mapped.disconnect();
        view_focused.disconnect();
        view_unmapped.disconnect();
        view_geometry_changed.disconnect();
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<window_swallow>);
}
