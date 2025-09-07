/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 Scott Moreau <oreaus@gmail.com>
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

#pragma once

#include <set>
#include <cmath>
#include <wayfire/core.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/common/util.hpp>

namespace wf
{
namespace dodge
{
static std::string dodge_transformer_name = "dodge";

// Simple structure for each dodging view
struct dodge_view_data
{
    wayfire_view view;
    wf::geometry_t bb;
    std::shared_ptr<wf::scene::view_2d_transformer_t> transformer;
    wf::pointf_t direction;
};

// Helper function to check if two boxes intersect
bool boxes_intersect(const wlr_box & a, const wlr_box & b)
{
    return !(a.x >= b.x + b.width ||
        b.x >= a.x + a.width ||
        a.y >= b.y + b.height ||
        b.y >= a.y + a.height);
}

class wayfire_dodge
{
    std::vector<dodge_view_data> views_from;
    wayfire_view view_to, last_focused_view;
    wf::option_wrapper_t<std::string> direction{"extra-animations/dodge_direction"};
    wf::option_wrapper_t<wf::animation_description_t> animation_duration{"extra-animations/dodge_duration"};
    wf::animation::simple_animation_t progression{animation_duration};
    bool view_to_focused;
    wf::output_t *view_to_output;
    bool hook_set = false;

  public:
    void init()
    {
        LOGI("init");
        wf::get_core().connect(&view_mapped);
        wf::get_core().connect(&view_unmapped);
        this->progression.set(0, 0);
        for (auto& view : wf::get_core().get_all_views())
        {
            if (wf::toplevel_cast(view))
            {
                view->connect(&view_activated);
            }
        }
    }

    wf::signal::connection_t<wf::view_activated_state_signal> view_activated =
        [=] (wf::view_activated_state_signal *ev)
    {
        LOGI("view_activated");
        if (ev->view == wf::get_core().seat->get_active_view())
        {
            last_focused_view = wf::get_core().seat->get_active_view();
            return;
        }

        view_to = ev->view;
        if (!last_focused_view || !view_to || (last_focused_view == view_to))
        {
            return;
        }

        auto toplevel = wf::toplevel_cast(view_to);
        auto to_bb    = toplevel->get_geometry();

        // Find overlapping views
        std::vector<wayfire_view> overlapping_views;
        for (auto& view : wf::get_core().get_all_views())
        {
            if (!view || (view == view_to) || !view->is_mapped())
            {
                continue;
            }

            toplevel = wf::toplevel_cast(view);
            if (!toplevel)
            {
                continue;
            }

            auto from_bb = toplevel->get_geometry();

            if ((wf::get_focus_timestamp(view_to) < wf::get_focus_timestamp(view)) &&
                boxes_intersect(to_bb, from_bb))
            {
                overlapping_views.push_back(view);
            }
        }

        if (overlapping_views.empty())
        {
            return;
        }

        // Keep the current focused view in front initially (this prevents jumping)
        view_bring_to_front(last_focused_view);

        if (!hook_set)
        {
            view_to_output = view_to->get_output();
            view_to_output->render->add_effect(&dodge_animation_hook, wf::OUTPUT_EFFECT_PRE);
            hook_set = true;
        }

        // Setup overlapping views with fan directions
        for (size_t i = 0; i < overlapping_views.size(); ++i)
        {
            dodge_view_data view_data;
            view_data.view = overlapping_views[i];
            if (auto tr = view_data.view->get_transformed_node()->get_transformer(dodge_transformer_name))
            {
                for (auto& vd : views_from)
                {
                    if (vd.transformer == tr)
                    {
                        auto direction = compute_direction(vd.view, view_to, vd.bb);

                        vd.direction.x = direction.x;
                        vd.direction.y = direction.y;
                        break;
                    }
                }

                continue;
            }

            view_data.bb = view_data.view->get_bounding_box();
            auto direction = compute_direction(overlapping_views[i], view_to, view_data.bb);

            view_data.direction.x = direction.x;
            view_data.direction.y = direction.y;

            view_data.transformer = std::make_shared<wf::scene::view_2d_transformer_t>(view_data.view);
            view_data.view->get_transformed_node()->add_transformer(view_data.transformer, wf::TRANSFORMER_2D,
                dodge_transformer_name);

            views_from.push_back(view_data);
        }

        view_to_focused = false;
        this->progression.animate(0, 1);
    };

    wf::signal::connection_t<wf::view_mapped_signal> view_mapped =
        [=] (wf::view_mapped_signal *ev)
    {
        ev->view->connect(&view_activated);
    };

    wf::signal::connection_t<wf::view_unmapped_signal> view_unmapped =
        [=] (wf::view_unmapped_signal *ev)
    {
        last_focused_view = wf::get_core().seat->get_active_view();
        if (ev->view == view_to)
        {
            view_to = nullptr;
        }

        views_from.erase(std::remove_if(views_from.begin(), views_from.end(),
            [ev] (const dodge_view_data& data)
        {
            return data.view == ev->view;
        }), views_from.end());
    };

    double magnitude(int x, int y)
    {
        return std::sqrt(x * x + y * y);
    }

    wf::pointf_t compute_direction(wayfire_view from, wayfire_view to, wf::geometry_t from_bb)
    {
        auto to_bb = to->get_bounding_box();
        auto from_center = wf::point_t{from_bb.x + from_bb.width / 2, from_bb.y + from_bb.height / 2};
        auto to_center   = wf::point_t{to_bb.x + to_bb.width / 2, to_bb.y + to_bb.height / 2};
        auto x = double(from_center.x - to_center.x);
        auto y = double(from_center.y - to_center.y);
        auto m = magnitude(x, y);
        if (m == 0)
        {
            return wf::pointf_t{0, 0};
        }

        auto nx = x / m;
        auto ny = y / m;
        x = nx * std::abs(1.0 / nx);
        y = ny * std::abs(1.0 / ny);
        return wf::pointf_t{x, y};
    }

    void damage_views()
    {
        for (auto& view_data : views_from)
        {
            view_data.view->damage();
        }

        if (view_to)
        {
            view_to->damage();
        }
    }

    void finish_animation()
    {
        for (auto& view_data : views_from)
        {
            view_data.view->get_transformed_node()->rem_transformer(dodge_transformer_name);
        }

        if (hook_set)
        {
            view_to_output->render->rem_effect(&dodge_animation_hook);
            hook_set = false;
        }

        views_from.clear();
        view_to = nullptr;
    }

    bool step_animation()
    {
        if (!view_to || !last_focused_view)
        {
            return false;
        }

        auto to_bb = view_to->get_bounding_box();

        double progress = progression.progress();
        progress = (1.0 - progress) * (1.0 - progress);
        progress = 1.0 - progress;

        // Animate overlapping views with speed-adjusted timing
        for (auto& view_data : views_from)
        {
            auto from_bb = view_data.bb;
            double move_dist_x = std::min(from_bb.x + from_bb.width - to_bb.x,
                to_bb.x + to_bb.width - from_bb.x);
            double move_dist_y = std::min(from_bb.y + from_bb.height - to_bb.y,
                to_bb.y + to_bb.height - from_bb.y);

            if (std::string(direction) == "cardinal")
            {
                if (move_dist_x < move_dist_y)
                {
                    move_dist_y = 0;
                } else
                {
                    move_dist_x = 0;
                }
            }

            double move_x = move_dist_x * view_data.direction.x;
            double move_y = move_dist_y * view_data.direction.y;

            view_data.transformer->translation_x = std::sin(progress * M_PI) * move_x;
            view_data.transformer->translation_y = std::sin(progress * M_PI) * move_y;
        }

        if ((progress > 0.5) && !view_to_focused)
        {
            wf::get_core().seat->focus_view(view_to);
            view_bring_to_front(view_to);
            view_to_focused = true;
        }

        return progression.running();
    }

    wf::effect_hook_t dodge_animation_hook = [=] ()
    {
        damage_views();
        bool result = step_animation();
        damage_views();

        if (!result)
        {
            finish_animation();
        }
    };

    void fini()
    {
        finish_animation();
        view_mapped.disconnect();
        view_unmapped.disconnect();
        view_activated.disconnect();
    }
};
}
}
