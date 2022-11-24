/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2022 Scott Moreau
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include "wayfire/view-transform.hpp"
#include "wayfire/workspace-manager.hpp"


class winzoom_t : public wf::view_2D
{
    wf::option_wrapper_t<bool> nearest_filtering{"winzoom/nearest_filtering"};
    wayfire_view view;

    wf::config::option_base_t::updated_callback_t filtering_changed = [=] ()
    {
        view->damage();
    };

  public:
    winzoom_t(wayfire_view view) : wf::view_2D(view)
    {
        nearest_filtering.set_callback(filtering_changed);
        this->view = view;
    }

    ~winzoom_t()
    {}

    void render_with_damage(wf::texture_t src_tex, wlr_box src_box,
        const wf::region_t& damage, const wf::render_target_t& target_fb) override
    {
        GL_CALL(glBindTexture(GL_TEXTURE_2D, src_tex.tex_id));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
            nearest_filtering ? GL_NEAREST : GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
            nearest_filtering ? GL_NEAREST : GL_LINEAR));
        wf::view_transformer_t::render_with_damage(src_tex, src_box, damage,
            target_fb);
    }
};

class wayfire_winzoom : public wf::plugin_interface_t
{
    wf::option_wrapper_t<wf::activatorbinding_t> inc_x_binding{
        "winzoom/inc_x_binding"};
    wf::option_wrapper_t<wf::activatorbinding_t> dec_x_binding{
        "winzoom/dec_x_binding"};
    wf::option_wrapper_t<wf::activatorbinding_t> inc_y_binding{
        "winzoom/inc_y_binding"};
    wf::option_wrapper_t<wf::activatorbinding_t> dec_y_binding{
        "winzoom/dec_y_binding"};
    wf::option_wrapper_t<bool> preserve_aspect{"winzoom/preserve_aspect"};
    wf::option_wrapper_t<wf::keybinding_t> modifier{"winzoom/modifier"};
    wf::option_wrapper_t<double> zoom_step{"winzoom/zoom_step"};

  public:
    void init() override
    {
        grab_interface->name = "winzoom";
        grab_interface->capabilities = 0;

        output->add_axis(modifier, &axis_cb);
        output->add_activator(inc_x_binding, &on_inc_x);
        output->add_activator(dec_x_binding, &on_dec_x);
        output->add_activator(inc_y_binding, &on_inc_y);
        output->add_activator(dec_y_binding, &on_dec_y);
    }

    bool update_winzoom(wayfire_view view, wf::point_t delta)
    {
        winzoom_t *transformer;
        wf::pointf_t zoom;

        if (!view)
        {
            return false;
        }

        if (!output->activate_plugin(grab_interface))
        {
            return false;
        }

        output->deactivate_plugin(grab_interface);

        auto layer = output->workspace->get_view_layer(view);

        if (layer & (wf::LAYER_BACKGROUND | wf::LAYER_TOP))
        {
            return false;
        }

        if (view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
        {
            return false;
        }

        if (!view->get_transformer("winzoom"))
        {
            view->add_transformer(std::make_unique<winzoom_t>(view), "winzoom");
        }

        transformer =
            dynamic_cast<winzoom_t*>(view->get_transformer("winzoom").get());

        zoom.x = transformer->scale_x;
        zoom.y = transformer->scale_y;

        if (preserve_aspect)
        {
            if ((delta.x <= 0) && (delta.y <= 0))
            {
                delta.x = delta.y = std::min(delta.x, delta.y);
            }

            if ((delta.x >= 0) && (delta.y >= 0))
            {
                delta.x = delta.y = std::max(delta.x, delta.y);
            }
        }

        zoom.x += zoom_step * delta.x;
        zoom.y += zoom_step * delta.y;

        zoom.x = std::max(1.0, zoom.x);
        zoom.y = std::max(1.0, zoom.y);

        if ((zoom.x == 1.0) && (zoom.y == 1.0))
        {
            view->pop_transformer("winzoom");
            return true;
        }

        if (transformer->scale_x != zoom.x)
        {
            transformer->scale_x = zoom.x;
        }

        if (transformer->scale_y != zoom.y)
        {
            transformer->scale_y = zoom.y;
        }

        output->render->damage_whole();

        return true;
    }

    wf::activator_callback on_inc_x = [=] (auto)
    {
        auto view = output->get_active_view();
        return update_winzoom(view, wf::point_t{1, 0});
    };

    wf::activator_callback on_dec_x = [=] (auto)
    {
        auto view = output->get_active_view();
        return update_winzoom(view, wf::point_t{-1, 0});
    };

    wf::activator_callback on_inc_y = [=] (auto)
    {
        auto view = output->get_active_view();
        return update_winzoom(view, wf::point_t{0, 1});
    };

    wf::activator_callback on_dec_y = [=] (auto)
    {
        auto view = output->get_active_view();
        return update_winzoom(view, wf::point_t{0, -1});
    };

    wf::axis_callback axis_cb = [=] (wlr_pointer_axis_event *ev)
    {
        auto view = wf::get_core().get_cursor_focus_view();
        if (ev->orientation == WLR_AXIS_ORIENTATION_VERTICAL)
        {
            auto delta = (int)-std::clamp(ev->delta, -1.0, 1.0);
            return update_winzoom(view, wf::point_t{delta, delta});
        }

        return false;
    };

    void fini() override
    {
        for (auto& view : output->workspace->get_views_in_layer(wf::ALL_LAYERS))
        {
            if (view->get_transformer("winzoom"))
            {
                view->pop_transformer("winzoom");
            }
        }

        output->rem_binding(&axis_cb);
        output->rem_binding(&on_inc_x);
        output->rem_binding(&on_dec_x);
        output->rem_binding(&on_inc_y);
        output->rem_binding(&on_dec_y);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_winzoom);
