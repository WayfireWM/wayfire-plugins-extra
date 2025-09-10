/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 Scott Moreau <oreaus@gmail.com>
 * Copyright (c) 2025 Andrew Pliatsikas <futurebytestore@gmail.com>
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
#include <cstdlib>
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

struct dodge_view_data
{
    wayfire_view view;
    wf::geometry_t from_bb, to_bb;
    std::shared_ptr<wf::scene::view_2d_transformer_t> transformer;
    wf::pointf_t direction;
};

bool boxes_intersect(const wayfire_view & a, const wayfire_view & b)
{
    auto ao = a->get_output();
    auto bo = b->get_output();
    if (!ao || !bo || (ao != bo))
    {
        return false;
    }

    auto a_bb = wf::toplevel_cast(a)->get_geometry();
    auto b_bb = wf::toplevel_cast(b)->get_geometry();
    return !(b_bb.x > a_bb.x + a_bb.width ||
        a_bb.x > b_bb.x + b_bb.width ||
        b_bb.y > a_bb.y + a_bb.height ||
        a_bb.y > b_bb.y + b_bb.height);
}

class wayfire_dodge
{
    std::vector<dodge_view_data> views_from;
    std::list<wayfire_view> minimized_views;
    wayfire_view view_to, last_focused_view;
    wf::option_wrapper_t<bool> dodge_zoom{"extra-animations/dodge_zoom"};
    wf::option_wrapper_t<bool> dodge_rotate{"extra-animations/dodge_rotate"};
    wf::option_wrapper_t<std::string> direction{"extra-animations/dodge_direction"};
    wf::option_wrapper_t<wf::animation_description_t> animation_duration{"extra-animations/dodge_duration"};
    wf::animation::simple_animation_t progression{animation_duration};
    bool view_to_focused;
    wf::output_t *view_to_output;
    bool hook_set = false;
    bool view_unminimized    = false;
    bool from_mapped_event   = false;
    bool from_unmapped_event = false;
    std::vector<wf::output_t*> outputs;

  public:
    void init()
    {
        wf::get_core().connect(&view_mapped);
        wf::get_core().connect(&view_unmapped);
        this->progression.set(0, 0);
        for (auto& view : wf::get_core().get_all_views())
        {
            if (wf::toplevel_cast(view))
            {
                view->connect(&view_activated);
                view->get_output()->connect(&view_minimize_request);
                if (std::find_if(outputs.begin(), outputs.end(), [=] (wf::output_t*& o)
                {
                    return view->get_output() == o;
                }) == outputs.end())
                {
                    outputs.push_back(view->get_output());
                }
            }
        }

        for (auto output : outputs)
        {
            output->connect(&view_minimize_request);
        }
    }

    wf::signal::connection_t<wf::view_minimize_request_signal> view_minimize_request =
        [=] (wf::view_minimize_request_signal *ev)
    {
        if (ev->state)
        {
            minimized_views.push_back(ev->view);
        } else
        {
            minimized_views.remove(ev->view);
            view_unminimized = true;
            wf::view_activated_state_signal data;
            data.view = ev->view;
            view_activated.emit(&data);
        }
    };

    wf::signal::connection_t<wf::view_activated_state_signal> view_activated =
        [=] (wf::view_activated_state_signal *ev)
    {
        if (ev->view == wf::get_core().seat->get_active_view())
        {
            last_focused_view = wf::get_core().seat->get_active_view();
            if (!from_mapped_event)
            {
                return;
            }
        }

        if (from_unmapped_event)
        {
            from_unmapped_event = false;
            return;
        }

        view_to = ev->view;

        auto toplevel = wf::toplevel_cast(view_to);
        if (!toplevel)
        {
            return;
        }

        if (!last_focused_view || !view_to || !view_to->is_mapped() ||
            toplevel->parent)
        {
            return;
        }

        auto to_bb = toplevel->get_geometry();

        if (from_mapped_event)
        {
            view_bring_to_front(view_to);
        }

        /* Find overlapping views */
        std::vector<wayfire_view> overlapping_views;
        auto all_views = wf::get_core().get_all_views();
        std::sort(all_views.begin(), all_views.end(), [] (const wayfire_view& a, const wayfire_view& b)
        {
            return wf::get_focus_timestamp(a) < wf::get_focus_timestamp(b);
        });
        for (auto& view : all_views)
        {
            if (!view || !view->is_mapped())
            {
                continue;
            }

            toplevel = wf::toplevel_cast(view);
            if (!toplevel)
            {
                continue;
            }

            if ((wf::get_focus_timestamp(view_to) < wf::get_focus_timestamp(view)) ||
                ((from_mapped_event || view_unminimized) &&
                 (wf::get_focus_timestamp(view_to) > wf::get_focus_timestamp(view))))
            {
                if ((boxes_intersect(view, view_to) || from_mapped_event ||
                     view->get_transformed_node()->get_transformer<wf::scene::view_2d_transformer_t>(
                         dodge_transformer_name)) &&
                    (std::find_if(minimized_views.begin(), minimized_views.end(),
                        [view] (const wayfire_view& v) { return v == view; }) == minimized_views.end()))
                {
                    overlapping_views.push_back(view);
                    view_bring_to_front(view);
                }
            }

            if (!from_mapped_event)
            {
                view_bring_to_front(view);
            }
        }

        view_unminimized = false;

        if (overlapping_views.empty())
        {
            return;
        }

        if (!hook_set)
        {
            view_to_output = view_to->get_output();
            view_to_output->render->add_effect(&dodge_animation_hook, wf::OUTPUT_EFFECT_PRE);
            hook_set = true;
        }

        view_to_focused = false;
        if (!this->progression.running())
        {
            views_from.clear();
            this->progression.animate(0, 1);
        }

        for (size_t i = 0; i < overlapping_views.size(); i++)
        {
            dodge_view_data view_data;
            view_data.view = overlapping_views[i];

            view_data.from_bb = view_data.view->get_bounding_box();
            view_data.to_bb   = view_to->get_bounding_box();

            bool view_found = false;
            for (auto vd : views_from)
            {
                if (vd.view == view_data.view)
                {
                    view_found = true;
                    if (dodge_rotate && from_mapped_event)
                    {
                        auto direction = compute_direction(view_data.from_bb, to_bb);
                        vd.direction.x = direction.x;
                        vd.direction.y = direction.y;
                        vd.transformer->angle = 0.1;
                    }

                    break;
                }
            }

            if (view_found)
            {
                continue;
            }

            if (auto tr =
                    view_data.view->get_transformed_node()->get_transformer<wf::scene::view_2d_transformer_t>(
                        dodge_transformer_name))
            {
                view_data.transformer = tr;
                if (dodge_rotate && from_mapped_event)
                {
                    view_data.transformer->angle = 0.1;
                }
            } else
            {
                view_data.transformer = std::make_shared<wf::scene::view_2d_transformer_t>(view_data.view);
                view_data.view->get_transformed_node()->add_transformer(view_data.transformer,
                    wf::TRANSFORMER_2D,
                    dodge_transformer_name);
                auto d = compute_direction(view_data.from_bb, to_bb);
                view_data.direction.x = d.x;
                view_data.direction.y = d.y;

                auto & x = view_data.direction.x;
                auto & y = view_data.direction.y;
                if ((((x < 0.001) && (x > -0.001)) && ((y < 0.001) && (y > -0.001))) || std::isnan(x) ||
                    std::isnan(y))
                {
                    if ((std::string(direction) == "cardinal") || (std::string(direction) == "diagonal"))
                    {
                        srand(wf::get_current_time() + rand());
                        x = (rand() % 2) * 2 - 1;
                        srand(wf::get_current_time() + rand());
                        y = (rand() % 2) * 2 - 1;
                    } else if (std::string(direction) == "circular")
                    {
                        srand(wf::get_current_time() + rand());
                        x = (double(rand()) / RAND_MAX) * 2.0 - 1.0;
                        srand(wf::get_current_time() + rand());
                        y = (double(rand()) / RAND_MAX) * 2.0 - 1.0;
                    }
                }

                if (dodge_rotate)
                {
                    view_data.transformer->angle = 0.1;
                }
            }

            views_from.push_back(view_data);
        }

        from_mapped_event = false;
    };

    wf::signal::connection_t<wf::view_mapped_signal> view_mapped =
        [=] (wf::view_mapped_signal *ev)
    {
        from_mapped_event = true;
        ev->view->connect(&view_activated);
        if (std::find_if(outputs.begin(), outputs.end(), [=] (wf::output_t*& o)
        {
            return ev->view->get_output() == o;
        }) == outputs.end())
        {
            ev->view->get_output()->connect(&view_minimize_request);
            outputs.push_back(ev->view->get_output());
        }
    };

    wf::signal::connection_t<wf::view_unmapped_signal> view_unmapped =
        [=] (wf::view_unmapped_signal *ev)
    {
        from_unmapped_event = true;
        last_focused_view   = wf::get_core().seat->get_active_view();
        if (ev->view == view_to)
        {
            view_bring_to_front(view_to);
            view_to = nullptr;
        }

        if (ev->view == last_focused_view)
        {
            last_focused_view = nullptr;
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

    wf::pointf_t compute_direction(wf::geometry_t from_bb, wf::geometry_t to_bb)
    {
        auto from_center = wf::point_t{from_bb.x + from_bb.width / 2, from_bb.y + from_bb.height / 2};
        auto to_center   = wf::point_t{to_bb.x + to_bb.width / 2, to_bb.y + to_bb.height / 2};
        auto x = double(from_center.x - to_center.x);
        auto y = double(from_center.y - to_center.y);
        auto m = magnitude(x, y);
        if (m == 0)
        {
            return wf::pointf_t{0, 0};
        }

        x /= m;
        y /= m;
        if (std::string(direction) != "circular")
        {
            x = x * std::abs(1.0 / x);
            y = y * std::abs(1.0 / y);
        }

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
        for (auto& view : wf::get_core().get_all_views())
        {
            view->get_transformed_node()->rem_transformer(dodge_transformer_name);
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
        if (!view_to)
        {
            return false;
        }

        double progress = progression.progress();
        progress = (1.0 - progress) * (1.0 - progress);
        progress = 1.0 - progress;

        std::shared_ptr<wf::scene::view_2d_transformer_t> view_to_transformer;
        if (std::find_if(minimized_views.begin(), minimized_views.end(), [&] (const wayfire_view& v)
        {
            return v == last_focused_view;
        }) == minimized_views.end())
        {
            if (auto tr =
                    view_to->get_transformed_node()->get_transformer<wf::scene::view_2d_transformer_t>(
                        dodge_transformer_name))
            {
                view_to_transformer = tr;
            } else
            {
                view_to_transformer = std::make_shared<wf::scene::view_2d_transformer_t>(view_to);
                view_to->get_transformed_node()->add_transformer(view_to_transformer, wf::TRANSFORMER_2D,
                    dodge_transformer_name);
            }
        } else
        {
            view_to_focused = true;
        }

        if (dodge_zoom && view_to_transformer)
        {
            view_to_transformer->scale_x = view_to_transformer->scale_y = 1.0 + std::sin(
                progression.progress() * M_PI) * 0.02;
        }

        for (auto& view_data : views_from)
        {
            auto to_bb   = view_data.to_bb;
            auto from_bb = view_data.from_bb;
            double move_dist_x = std::min(from_bb.x + from_bb.width - to_bb.x,
                to_bb.x + to_bb.width - from_bb.x);
            double move_dist_y = std::min(from_bb.y + from_bb.height - to_bb.y,
                to_bb.y + to_bb.height - from_bb.y);

            if (dodge_zoom)
            {
                if (view_data.transformer->scale_x <= 1.0)
                {
                    view_data.transformer->scale_x = view_data.transformer->scale_y = 1.0 - std::sin(
                        progression.progress() * M_PI) * 0.25;
                } else
                {
                    view_data.transformer->scale_x = view_data.transformer->scale_y = 1.0 + std::sin(
                        progression.progress() * M_PI) * 0.02;
                }
            }

            if (dodge_rotate && (view_data.transformer->angle != 0.0))
            {
                view_data.transformer->angle = progress * M_PI * 2 * (view_data.direction.x > 0 ? 1 : -1);
                if (view_data.transformer->angle == 0.0)
                {
                    view_data.transformer->angle = 0.1;
                }
            }

            if (std::string(direction) == "cardinal")
            {
                if (move_dist_x < move_dist_y)
                {
                    move_dist_y = 0;
                } else
                {
                    move_dist_x = 0;
                }
            } else if (std::string(direction) == "circular")
            {
                auto direction_x = std::abs(view_data.direction.x);
                auto direction_y = std::abs(view_data.direction.y);
                if (direction_x < direction_y)
                {
                    move_dist_x *= direction_x;
                    auto& y = view_data.direction.y;
                    y = y * std::abs(1.0 / y);
                } else
                {
                    move_dist_y *= direction_y;
                    auto& x = view_data.direction.x;
                    x = x * std::abs(1.0 / x);
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
