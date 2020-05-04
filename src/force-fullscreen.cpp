/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Ilia Bozhinov
 * Copyright (c) 2020 Scott Moreau
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

#include <map>
#include <unordered_set>
#include <wayfire/core.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/compositor-view.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>

#include <glm/gtc/matrix_transform.hpp>


class fullscreen_subsurface : public wf::surface_interface_t, public wf::compositor_surface_t
{
  public:
    bool _mapped = true;
    fullscreen_subsurface(wayfire_view view)
        : wf::surface_interface_t(), wf::compositor_surface_t() {}

    ~fullscreen_subsurface() {}

    void on_pointer_enter(int x, int y) override
    {
        wf::get_core().set_cursor("default");
    }

    bool accepts_input(int32_t sx, int32_t sy) override
    {
        return wlr_box{0, 0, 1, 1} & wf::point_t{sx, sy};
    }

    bool is_mapped() const override
    {
        return _mapped;
    }

    wf::point_t get_offset() override
    {
        /* TODO: support below the surface as well, if wider than screen */
        return {-1, 0};
    }

    wf::dimensions_t get_size() const override
    {
        return {1, 1};
    }

    void simple_render(const wf::framebuffer_t& fb, int x, int y,
        const wf::region_t& damage) override
    { /* fully transparent */ }
};

class fullscreen_transformer : public wf::view_2D
{
  public:
    wayfire_view view;
    wlr_box transformed_view_box;

    fullscreen_transformer(wayfire_view view) : wf::view_2D(view)
    {
        this->view = view;
    }

    ~fullscreen_transformer() {}

    wf::point_t get_workspace(wlr_box og)
    {
        wf::point_t ws;
        auto vg = view->get_output_geometry();
        wf::pointf_t center{vg.x + vg.width / 2.0, vg.y + vg.height / 2.0};

        ws.x = std::floor(center.x / og.width);
        ws.y = std::floor(center.y / og.height);

        return ws;
    }

    /* TODO: transform_point */
    wf::geometry_t get_bounding_box(
        wf::geometry_t geometry, wf::geometry_t box) override
    {
        auto output = view->get_output();
        auto bbox = wf::view_2D::get_bounding_box(geometry, box);

        if (!output)
        {
            return bbox;
        }

        wf::geometry_t wm = view->get_wm_geometry();
        wf::point_t subsurface = {wm.x - 1, wm.y};
        auto og = output->get_relative_geometry();
        auto ws = get_workspace(og);
        if (box & subsurface) {
            og.x += ws.x * og.width;
            og.y += ws.y * og.height;
            return og;
        } else {
            bbox.x += ws.x * og.width;
            bbox.y += ws.y * og.height;
            return bbox;
        }
    }

    wlr_box get_relative_transformed_view_box(wlr_box& og)
    {
        auto ws = get_workspace(og);
        auto bbox = transformed_view_box;
        bbox.x += ws.x * og.width;
        bbox.y += ws.y * og.height;
        og.x += ws.x * og.width;
        og.y += ws.y * og.height;
        return bbox;
    }

    wf::pointf_t untransform_point(
        wf::geometry_t geometry, wf::pointf_t point) override
    {
        auto output = view->get_output();
        auto default_point = wf::view_2D::untransform_point(geometry, point);

        if (!output)
        {
            return default_point;
        }

        auto og = output->get_relative_geometry();
        auto bbox = get_relative_transformed_view_box(og);

        if (!(bbox & point))
        {
            auto region = ((wf::region_t{og} ^ wf::region_t{bbox}) &
                wf::region_t{{(int) point.x, (int) point.y, 1, 1}});
            if (!region.empty())
            {
                auto wm = view->get_wm_geometry();
                return wf::pointf_t{wm.x - 1.0, wm.y * 1.0};
            }
        }
        return default_point;
    }

    void render_box(wf::texture_t src_tex, wlr_box src_box,
        wlr_box scissor_box, const wf::framebuffer_t& target_fb) override
    {
        auto output = view->get_output();

        if (!output)
        {
            return;
        }

        auto og = output->get_relative_geometry();
        auto bbox = get_relative_transformed_view_box(og);
        wf::region_t vr{bbox};
        wf::region_t scissor_region{scissor_box};
        scissor_region ^= vr;
        for (auto& b : scissor_region)
        {
            OpenGL::render_begin(target_fb);
            target_fb.logic_scissor(wlr_box_from_pixman_box(b));
            OpenGL::clear({0, 0, 0, 1});
            OpenGL::render_end();
        }

        wf::view_2D::render_box(src_tex, src_box, scissor_box, target_fb);
    }
};

class fullscreen_background
{
  public:
    wf::geometry_t saved_geometry;
    wf::geometry_t undecorated_geometry;
    fullscreen_transformer *transformer;
    fullscreen_subsurface *black_border = nullptr;

    fullscreen_background(wayfire_view view) {}

    ~fullscreen_background() {}
};

class wayfire_force_fullscreen;

std::map<wf::output_t*, wayfire_force_fullscreen*> wayfire_force_fullscreen_instances;

class wayfire_force_fullscreen : public wf::plugin_interface_t
{
    double saved_wrot_angle;
    std::string background_name;
    wf::view_2D *wrot_transformer;
    std::map<wayfire_view, std::unique_ptr<fullscreen_background>> backgrounds;
    wf::option_wrapper_t<bool> preserve_aspect{"force-fullscreen/preserve_aspect"};
    wf::option_wrapper_t<bool> transparent_behind_views{"force-fullscreen/transparent_behind_views"};
    wf::option_wrapper_t<wf::keybinding_t> key_toggle_fullscreen{"force-fullscreen/key_toggle_fullscreen"};

  public:
    void init() override
    {
        this->grab_interface->name = "force-fullscreen";
        this->grab_interface->capabilities = 0;
        background_name = this->grab_interface->name;

        output->add_key(key_toggle_fullscreen, &on_toggle_fullscreen);
        preserve_aspect.set_callback(option_changed);
        transparent_behind_views.set_callback(option_changed);
        wayfire_force_fullscreen_instances[output] = this;
    }

    void ensure_subsurface(wayfire_view view)
    {
        auto pair = backgrounds.find(view);

        if (pair == backgrounds.end())
        {
            return;
        }

        auto& background = pair->second;

        if (!background->black_border)
        {
            std::unique_ptr<fullscreen_subsurface> subsurface = std::make_unique<fullscreen_subsurface>(view);
            nonstd::observer_ptr<fullscreen_subsurface> ptr{subsurface};
            view->add_subsurface(std::move(subsurface), true);
            background->black_border = ptr.get();
        }
    }

    void destroy_subsurface(wayfire_view view)
    {
        auto pair = backgrounds.find(view);

        if (pair == backgrounds.end())
        {
            return;
        }

        auto& background = pair->second;

        if (background->black_border)
        {
            wf::emit_map_state_change(background->black_border);
            background->black_border->_mapped = false;
            view->remove_subsurface(background->black_border);
            background->black_border = nullptr;
        }
    }

    void setup_transform(wayfire_view view)
    {
        auto og = output->get_relative_geometry();
        auto vg = view->get_wm_geometry();

        double scale_x = (double) og.width / (vg.width - 1);
        double scale_y = (double) og.height / vg.height;
        double translation_x = (og.width - vg.width - 1) / 2.0;
        double translation_y = (og.height - vg.height - 1) / 2.0;

        if (preserve_aspect)
        {
            scale_x = std::min(scale_x, scale_y);
            scale_y = std::min(scale_x, scale_y);
        }

        wlr_box box;
        if (transparent_behind_views)
        {
            box.width = (vg.width - 1) * scale_x;
            box.height = (vg.height - 1) * scale_y;
            box.x = (og.width - box.width) / 2.0;
            box.y = (og.height - box.height) / 2.0;
        }
        else
        {
            box = {0, 0, 0, 0};
        }

        backgrounds[view]->transformer->transformed_view_box = box;
        backgrounds[view]->transformer->scale_x = scale_x;
        backgrounds[view]->transformer->scale_y = scale_y;
        backgrounds[view]->transformer->translation_x = translation_x;
        backgrounds[view]->transformer->translation_y = translation_y;

        if (preserve_aspect)
        {
            ensure_subsurface(view);
        }
        else
        {
            destroy_subsurface(view);
        }

        view->damage();
    }

    void update_backgrounds()
    {
        for (auto& b : backgrounds)
        {
            destroy_subsurface(b.first);
            setup_transform(b.first);
        }
    }

    wf::config::option_base_t::updated_callback_t option_changed = [=] ()
    {
        update_backgrounds();
    };

    bool toggle_fullscreen(wayfire_view view)
    {
        if (!output->activate_plugin(grab_interface))
        {
            return false;
        }

        wlr_box saved_geometry = view->get_wm_geometry();

        auto background = backgrounds.find(view);
        bool fullscreen = background == backgrounds.end() ? true : false;

        view->set_fullscreen(fullscreen);

        wlr_box undecorated_geometry = view->get_wm_geometry();

        if (!fullscreen)
        {
            deactivate(view);
            return true;
        }

        activate(view);

        background = backgrounds.find(view);
        if (background != backgrounds.end())
        {
            background->second->undecorated_geometry = undecorated_geometry;
            background->second->saved_geometry = saved_geometry;
        }

        setup_transform(view);

        return true;
    }

    wf::key_callback on_toggle_fullscreen = [=] (uint32_t key)
    {
        auto view = output->get_active_view();

        if (!view)
        {
            return false;
        }

        return toggle_fullscreen(view);
    };

    void activate(wayfire_view view)
    {
        view->move(0, 0);
        backgrounds[view] = std::make_unique<fullscreen_background>(view);
        backgrounds[view]->transformer = new fullscreen_transformer(view);
        view->add_transformer(std::unique_ptr<fullscreen_transformer>(backgrounds[view]->transformer), background_name);
        output->connect_signal("output-configuration-changed", &output_config_changed);
        wf::get_core().connect_signal("view-move-to-output", &view_output_changed);
        output->connect_signal("view-fullscreen-request", &view_fullscreened);
        view->connect_signal("geometry-changed", &view_geometry_changed);
        output->connect_signal("unmap-view", &view_unmapped);
        output->deactivate_plugin(grab_interface);

        if (view->get_transformer("wrot"))
        {
            wrot_transformer = dynamic_cast<wf::view_2D*> (view->get_transformer("wrot").get());
            saved_wrot_angle = wrot_transformer->angle;
            wrot_transformer->angle = 0.0;
        }
    }

    void deactivate(wayfire_view view)
    {
        output->deactivate_plugin(grab_interface);
        auto background = backgrounds.find(view);

        if (background == backgrounds.end())
        {
            return;
        }

        if (backgrounds.size() == 1)
        {
            view_geometry_changed.disconnect();
            output_config_changed.disconnect();
            view_output_changed.disconnect();
            view_fullscreened.disconnect();
            view_unmapped.disconnect();
        }
        auto og = output->get_relative_geometry();
        auto ws = background->second->transformer->get_workspace(og);
        view->move(
            background->second->saved_geometry.x + ws.x * og.width,
            background->second->saved_geometry.y + ws.y * og.height);
        if (view->get_transformer(background_name))
        {
            view->pop_transformer(background_name);
        }

        if (view->get_transformer("wrot"))
        {
            wrot_transformer->angle = saved_wrot_angle;
        }
        destroy_subsurface(view);
        backgrounds.erase(view);
    }

    wf::signal_connection_t output_config_changed{[this] (wf::signal_data_t *data)
    {
        wf::output_configuration_changed_signal *signal =
            static_cast<wf::output_configuration_changed_signal*>(data);

        if (!signal->changed_fields)
        {
            return;
        }

        if (signal->changed_fields & wf::OUTPUT_SOURCE_CHANGE)
        {
            return;
        }

        update_backgrounds();
    }};

    wf::signal_connection_t view_output_changed{[this] (wf::signal_data_t *data)
    {
        auto signal = static_cast<wf::view_move_to_output_signal*> (data);
        auto view = signal->view;
        auto background = backgrounds.find(view);

        if (background == backgrounds.end())
        {
            return;
        }

        toggle_fullscreen(view);

        auto instance = wayfire_force_fullscreen_instances[signal->new_output];
        instance->toggle_fullscreen(view);
    }};

    wf::signal_connection_t view_unmapped{[this] (wf::signal_data_t *data)
    {
        auto view = get_signaled_view(data);
        auto background = backgrounds.find(view);

        if (background == backgrounds.end())
        {
            return;
        }

        toggle_fullscreen(view);
    }};

    wf::signal_connection_t view_fullscreened{[this] (wf::signal_data_t *data)
    {
        auto signal = static_cast<view_fullscreen_signal*> (data);
        auto view = signal->view;
        auto background = backgrounds.find(view);

        if (background == backgrounds.end())
        {
            return;
        }

        if (signal->state || signal->carried_out)
        {
            return;
        }

        toggle_fullscreen(view);

        signal->carried_out = true;
    }};

    wf::signal_connection_t view_geometry_changed{[this] (wf::signal_data_t *data)
    {
        auto view = get_signaled_view(data);
        auto background = backgrounds.find(view);

        if (background == backgrounds.end())
        {
            return;
        }

        view->resize(
            background->second->undecorated_geometry.width,
            background->second->undecorated_geometry.height);
        setup_transform(view);
    }};

    void fini() override
    {
        output->rem_binding(&on_toggle_fullscreen);
        wayfire_force_fullscreen_instances.erase(output);

        for (auto& b : backgrounds)
        {
            toggle_fullscreen(b.first);
        }
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_force_fullscreen);
