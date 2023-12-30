/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Edwin Ljung
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT
 * LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
 * EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <wayfire/bindings.hpp>
#include <wayfire/config/option.hpp>
#include <wayfire/core.hpp>
#include <wayfire/nonstd/tracking-allocator.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/config/types.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/bindings-repository.hpp>
#include <wayfire/window-manager.hpp>

namespace focus_change
{
enum class orientation_t
{
    UP,
    DOWN,
    RIGHT,
    LEFT,
};

class wayfire_focus_change_t : public wf::plugin_interface_t
{
  private:
    wf::option_wrapper_t<wf::keybinding_t> key_up{"focus-change/up"},
    key_down{"focus-change/down"},
    key_right{"focus-change/right"},
    key_left{"focus-change/left"};
    wf::option_wrapper_t<int> grace_up{"focus-change/grace-up"},
    grace_down{"focus-change/grace-down"},
    grace_right{"focus-change/grace-right"},
    grace_left{"focus-change/grace-left"};
    wf::option_wrapper_t<bool> cross_outputs{"focus-change/cross-output"};
    wf::option_wrapper_t<bool> cross_workspace{"focus-change/cross-workspace"};
    wf::option_wrapper_t<bool> raise_on_change{"focus-change/raise-on-change"};
    wf::option_wrapper_t<int> scan_height{"focus-change/scan-height"},
    scan_width{"focus-change/scan-width"};


    void change_focus(orientation_t orientation)
    {
        const auto cur_view   = wf::get_core().seat->get_active_view();
        const auto cur_output = cur_view->get_output();
        const auto cur_bb     = cur_view->get_bounding_box();
        const int32_t cur_cx  = cur_bb.x + cur_bb.width / 2;
        const int32_t cur_cy  = cur_bb.y + cur_bb.height / 2;
        wf::toplevel_view_interface_t *new_focus = nullptr;

        if (cur_output == nullptr)
        {
            return;
        }

        const bool cross_ws  = cross_workspace.value();
        const auto workspace =
            cross_ws ? std::optional<wf::point_t>{} : std::optional{cur_output->wset()->
            get_current_workspace()};

        auto iterating_output = cur_output->wset()->get_views(0, workspace);
        int32_t closest_cur   = INT32_MAX;
        for (auto&& view : std::move(iterating_output))
        {
            if (view->get_id() == cur_view->get_id())
            {
                continue;
            }

            const auto bb     = view->get_bounding_box();
            const auto output = view->get_output();
            if (output == nullptr)
            {
                continue;
            }

            const auto lg     = output->get_layout_geometry();
            const int32_t cxr = bb.width / 2;
            const int32_t cyr = bb.height / 2;
            const int32_t cx  = bb.x + cxr;
            const int32_t cy  = bb.y + cyr;

            if (!cross_ws && ((cx < 0) || (cx >= lg.width) || (cy < 0) || (cy >= lg.height)))
            {
                continue;
            }

            const int32_t scan_w_intrm = scan_width.value() > 0 ?
                scan_width.value() : scan_width.value() < 0 ?
                cur_bb.width - abs(scan_width.value()) : cur_bb.width;
            const int32_t scan_h_intrm = scan_height.value() > 0 ?
                scan_height.value() : scan_height.value() < 0 ?
                cur_bb.height - abs(scan_height.value()) : cur_bb.height;

            const int32_t scan_w = std::max(scan_w_intrm / 2, 1);
            const int32_t scan_h = std::max(scan_h_intrm / 2, 1);

            const int32_t scan_w_l = cur_cx - scan_w;
            const int32_t scan_w_h = cur_cx + scan_w;

            const int32_t scan_h_l = cur_cy - scan_h;
            const int32_t scan_h_h = cur_cy + scan_h;

            const int32_t bias_up    = grace_up.value();
            const int32_t bias_down  = grace_down.value();
            const int32_t bias_right = grace_right.value();
            const int32_t bias_left  = grace_left.value();

            const bool w_cond = cx + cxr >= scan_w_l && cx - cxr <= scan_w_h;
            const bool h_cond = cy + cyr >= scan_h_l && cy - cyr <= scan_h_h;

            bool contains    = false;
            int32_t distance = INT32_MAX;
            switch (orientation)
            {
              case orientation_t::UP:
                contains = w_cond;
                distance = cur_cy - cy - bias_up;
                break;

              case orientation_t::DOWN:
                contains = w_cond;
                distance = cy - cur_cy - bias_down;
                break;

              case orientation_t::RIGHT:
                contains = h_cond;
                distance = cx - cur_cx - bias_right;
                break;

              case orientation_t::LEFT:
                contains = h_cond;
                distance = cur_cx - cx - bias_left;
                break;

              default: /* unreachable */
                ;
            }

            if ((distance >= 0) && contains && (distance < closest_cur))
            {
                closest_cur = distance;
                new_focus   = view.get();
            }
        }

        if ((new_focus == nullptr) && cross_outputs.value())
        {
            const auto pos = cur_output->get_layout_geometry();

            auto outputs = wf::get_core().output_layout->get_outputs();
            wf::output_t *output   = nullptr;
            int32_t closest_output = INT32_MAX;
            for (auto op : outputs)
            {
                if (op->get_id() == cur_output->get_id())
                {
                    continue;
                }

                const auto cmp_pos = op->get_layout_geometry();

                const int32_t cx = cmp_pos.x + cmp_pos.width / 2;
                const int32_t cy = cmp_pos.y + cmp_pos.height / 2;

                const bool align_x = cx >= pos.x && cx < pos.x + pos.width;
                const bool align_y = cy >= pos.y && cy < pos.y + pos.height;

                int32_t distance = INT32_MAX;
                bool cond = false;
                switch (orientation)
                {
                  case orientation_t::UP:
                    cond     = align_x;
                    distance = pos.y - cmp_pos.y;
                    break;

                  case orientation_t::DOWN:
                    cond     = align_x;
                    distance = cmp_pos.y - pos.y;
                    break;

                  case orientation_t::RIGHT:
                    cond     = align_y;
                    distance = cmp_pos.x - pos.x;
                    break;

                  case orientation_t::LEFT:
                    cond     = align_y;
                    distance = pos.x - cmp_pos.x;
                    break;

                  default: /* unreachable */
                    ;
                }

                if (cond && (distance > 0) && (distance < closest_output))
                {
                    closest_output = distance;
                    output = op;
                }
            }

            if ((output != nullptr))
            {
                const auto cur_ws = output->wset()->get_current_workspace();

                auto views = output->wset()->get_views(0, std::optional{cur_ws});
                int32_t closest_cur = INT32_MAX;
                for (auto&& view : std::move(views))
                {
                    const auto bb = view.get()->get_bounding_box();
                    const auto cx = bb.x + bb.width / 2;
                    const auto cy = bb.y + bb.height / 2;
                    if ((cx < 0) || (cx >= pos.width) || (cy < 0) || (cy >= pos.height))
                    {
                        continue;
                    }

                    int32_t distance = INT32_MAX;
                    switch (orientation)
                    {
                      case orientation_t::UP:
                        distance = pos.height - cy;
                        break;

                      case orientation_t::DOWN:
                        distance = cy;
                        break;

                      case orientation_t::RIGHT:
                        distance = cx;
                        break;

                      case orientation_t::LEFT:
                        distance = pos.width - cx;
                        break;

                      default: /* unreachable */
                        ;
                    }

                    if ((distance >= 0) && (distance < closest_cur))
                    {
                        closest_cur = distance;
                        new_focus   = view.get();
                    }
                }
            }
        }

        if (new_focus != nullptr)
        {
            auto op = new_focus->get_output();
            if (op != nullptr)
            {
                const auto ws = op->wset()->get_view_main_workspace(new_focus);
                op->wset()->request_workspace(ws);
                wf::get_core().seat->focus_output(op);
            }

            wf::get_core().seat->focus_view(new_focus->self());
            if (raise_on_change.value())
            {
                wf::get_core().default_wm->focus_request(new_focus);
            }
        }
    }

    wf::key_callback on_key_up = [=] (auto)
    {
        change_focus(orientation_t::UP);
        return true;
    };

    wf::key_callback on_key_down = [=] (auto)
    {
        change_focus(orientation_t::DOWN);
        return true;
    };

    wf::key_callback on_key_right = [=] (auto)
    {
        change_focus(orientation_t::RIGHT);
        return true;
    };

    wf::key_callback on_key_left = [=] (auto)
    {
        change_focus(orientation_t::LEFT);
        return true;
    };

    void bind()
    {
        auto& core = wf::get_core();
        core.bindings->add_key(key_up, &on_key_up);
        core.bindings->add_key(key_down, &on_key_down);
        core.bindings->add_key(key_right, &on_key_right);
        core.bindings->add_key(key_left, &on_key_left);
    }

  public:
    void init() override
    {
        bind();
    }

    void fini() override
    {
        auto& core = wf::get_core();
        core.bindings->rem_binding(&on_key_up);
        core.bindings->rem_binding(&on_key_down);
        core.bindings->rem_binding(&on_key_right);
        core.bindings->rem_binding(&on_key_left);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_focus_change_t);
}
