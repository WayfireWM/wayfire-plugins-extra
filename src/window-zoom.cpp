/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2023 Scott Moreau
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
#include <wayfire/scene.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/view.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/per-output-plugin.hpp>


namespace wf
{
namespace scene
{
namespace winzoom
{
static wf::pointf_t get_center(wf::geometry_t view)
{
    return {
        view.x + view.width / 2.0,
        view.y + view.height / 2.0,
    };
}

class simple_node_render_instance_t : public transformer_render_instance_t<transformer_base_node_t>
{
    wf::signal::connection_t<node_damage_signal> on_node_damaged =
        [=] (node_damage_signal *ev)
    {
        push_damage(ev->region);
    };

    node_t *self;
    wayfire_toplevel_view view;
    float *scale_x, *scale_y;
    wlr_box *transformed_view_geometry;
    damage_callback push_damage;
    wf::option_wrapper_t<bool> nearest_filtering{"winzoom/nearest_filtering"};

  public:
    simple_node_render_instance_t(transformer_base_node_t *self, damage_callback push_damage,
        wayfire_toplevel_view view, float *scale_x, float *scale_y,
        wlr_box *transformed_view_geometry) :
        transformer_render_instance_t<transformer_base_node_t>(self, push_damage,
            view->get_output())
    {
        this->self    = self;
        this->view    = view;
        this->scale_x = scale_x;
        this->scale_y = scale_y;
        this->transformed_view_geometry = transformed_view_geometry;
        this->push_damage = push_damage;
        self->connect(&on_node_damaged);

        wf::config::option_base_t::updated_callback_t option_changed = [=] ()
        {
            this->view->damage();
        };

        nearest_filtering.set_callback(option_changed);
    }

    void schedule_instructions(
        std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        // We want to render ourselves only, the node does not have children
        instructions.push_back(render_instruction_t{
                        .instance = this,
                        .target   = target,
                        .damage   = damage & self->get_bounding_box(),
                    });
    }

    void transform_damage_region(wf::region_t& damage) override
    {
        damage |= view->get_transformed_node()->get_children_bounding_box();
    }

    wlr_box get_scaled_geometry()
    {
        auto vg = view->get_geometry();
        auto midpoint = get_center(vg);
        auto result   = wf::pointf_t{float(vg.x), float(vg.y)} - midpoint;
        result.x *= *scale_x;
        result.y *= *scale_y;
        result   += midpoint;
        transformed_view_geometry->x     = result.x;
        transformed_view_geometry->y     = result.y;
        transformed_view_geometry->width = vg.width * *scale_x;
        transformed_view_geometry->height = vg.height * *scale_y;
        return *transformed_view_geometry;
    }

    void render(const wf::scene::render_instruction_t& data) override
    {
        auto src_tex = get_texture(1.0);
        auto scaled_geometry = get_scaled_geometry();
        src_tex.filter_mode = nearest_filtering ? WLR_SCALE_FILTER_NEAREST : WLR_SCALE_FILTER_BILINEAR;
        data.pass->add_texture(src_tex, data.target, scaled_geometry, data.damage);
    }
};

class winzoom_t : public view_2d_transformer_t
{
    wayfire_toplevel_view view;
    wlr_box transformed_view_geometry;

  public:
    winzoom_t(wayfire_toplevel_view view) : view_2d_transformer_t(view)
    {
        this->view = view;
        this->transformed_view_geometry = view->get_geometry();
    }

    wf::pointf_t to_local(const wf::pointf_t& point) override
    {
        auto midpoint = get_center(transformed_view_geometry);
        auto result   = point - midpoint;
        result.x /= scale_x;
        result.y /= scale_y;
        result   += midpoint;
        return result;
    }

    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override
    {
        // push_damage accepts damage in the parent's coordinate system
        // If the node is a transformer, it may transform the damage. However,
        // this simple nodes does not need any transformations, so the push_damage
        // callback is just passed along.
        instances.push_back(std::make_unique<simple_node_render_instance_t>(
            this, push_damage, view, &scale_x, &scale_y,
            &transformed_view_geometry));
    }

    ~winzoom_t()
    {}
};

class wayfire_winzoom : public wf::per_output_plugin_instance_t
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
    std::map<wayfire_view, std::shared_ptr<winzoom_t>> transformers;
    wf::plugin_activation_data_t grab_interface{
        .name = "window-zoom",
        .capabilities = 0,
    };

  public:
    void init() override
    {
        output->add_axis(modifier, &axis_cb);
        output->add_activator(inc_x_binding, &on_inc_x);
        output->add_activator(dec_x_binding, &on_dec_x);
        output->add_activator(inc_y_binding, &on_inc_y);
        output->add_activator(dec_y_binding, &on_dec_y);
    }

    bool update_winzoom(wayfire_toplevel_view view, wf::point_t delta)
    {
        winzoom_t *transformer;
        wf::pointf_t zoom;

        if (!view)
        {
            return false;
        }

        if (!output->activate_plugin(&grab_interface))
        {
            return false;
        }

        output->deactivate_plugin(&grab_interface);

        auto layer = wf::get_view_layer(view);
        if ((layer == wf::scene::layer::BACKGROUND) || (layer == wf::scene::layer::TOP))
        {
            return false;
        }

        if (view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
        {
            return false;
        }

        if (!view->get_transformed_node()->get_transformer("winzoom"))
        {
            transformers[view] = std::make_shared<winzoom_t>(view);
            view->get_transformed_node()->add_transformer(transformers[view],
                wf::TRANSFORMER_2D, "winzoom");
        }

        transformer =
            dynamic_cast<winzoom_t*>(view->get_transformed_node()->get_transformer(
                "winzoom").get());

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
            view->get_transformed_node()->rem_transformer(transformers[view]);
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
        auto view = toplevel_cast(wf::get_active_view_for_output(output));
        return update_winzoom(view, wf::point_t{1, 0});
    };

    wf::activator_callback on_dec_x = [=] (auto)
    {
        auto view = toplevel_cast(wf::get_active_view_for_output(output));
        return update_winzoom(view, wf::point_t{-1, 0});
    };

    wf::activator_callback on_inc_y = [=] (auto)
    {
        auto view = toplevel_cast(wf::get_active_view_for_output(output));
        return update_winzoom(view, wf::point_t{0, 1});
    };

    wf::activator_callback on_dec_y = [=] (auto)
    {
        auto view = toplevel_cast(wf::get_active_view_for_output(output));
        return update_winzoom(view, wf::point_t{0, -1});
    };

    wf::axis_callback axis_cb = [=] (wlr_pointer_axis_event *ev)
    {
        auto view = toplevel_cast(wf::get_core().get_cursor_focus_view());
        if (ev->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL)
        {
            auto delta = (int)-std::clamp(ev->delta, -1.0, 1.0);
            return update_winzoom(view, wf::point_t{delta, delta});
        }

        return false;
    };

    void fini() override
    {
        for (auto& t : transformers)
        {
            t.first->get_transformed_node()->rem_transformer(t.second);
        }

        output->rem_binding(&axis_cb);
        output->rem_binding(&on_inc_x);
        output->rem_binding(&on_dec_x);
        output->rem_binding(&on_inc_y);
        output->rem_binding(&on_dec_y);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_winzoom>);
}
}
}
