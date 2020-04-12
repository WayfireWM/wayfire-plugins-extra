/*
 * The MIT License (MIT)
 *
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
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>

class fullscreen_transformer : public wf::view_2D
{
    wf::option_wrapper_t<bool> preserve_aspect{"force-fullscreen/preserve_aspect"};

  public:
    bool fullscreen;
    wayfire_view view;
    wf::point_t workspace;
    wlr_box output_geometry;
    wf::geometry_t saved_geometry;

    fullscreen_transformer(wayfire_view view) : wf::view_2D(view)
    {
        this->view = view;
        fullscreen = false;
    }

    ~fullscreen_transformer() { }

    void setup_transform(wlr_box og)
    {
        auto vg = view->get_wm_geometry();

        output_geometry = og;
        scale_x = (double) og.width / vg.width;
        scale_y = (double) og.height / vg.height;
        translation_x = (og.width - vg.width) / 2.0;
        translation_y = (og.height - vg.height) / 2.0;
    }

    wf::point_t get_workspace(wlr_box og)
    {
        wf::point_t ws;
        auto vg = view->get_output_geometry();
        wf::pointf_t center{vg.x + vg.width / 2.0, vg.y + vg.height / 2.0};

        ws.x = std::floor(center.x / og.width);
        ws.y = std::floor(center.y / og.height);

        return ws;
    }

    wlr_box get_workspace_geometry(wlr_box og)
    {
        wf::point_t ws = workspace = get_workspace(og);

        og.x += ws.x * og.width;
        og.y += ws.y * og.height;

        return og;
    }

    wlr_box get_bounding_box(wf::geometry_t view, wlr_box region) override
    {
        return get_workspace_geometry(output_geometry);
    }

    void render_box(wf::texture_t src_tex, wlr_box src_box,
        wlr_box scissor_box, const wf::framebuffer_t& target_fb) override
    {
        wf::pointf_t saved_scale;

        OpenGL::render_begin(target_fb);
        target_fb.logic_scissor(scissor_box);
        OpenGL::clear({0, 0, 0, 1});
        OpenGL::render_end();

        if (preserve_aspect)
        {
            saved_scale = wf::pointf_t{scale_x, scale_y};
            if (scale_x > scale_y)
            {
                scale_x = scale_y;
            }
            else if (scale_x < scale_y)
            {
                scale_y = scale_x;
            }
        }

        wf::view_2D::render_box(src_tex, src_box, scissor_box, target_fb);

        if (preserve_aspect)
        {
            scale_x = saved_scale.x;
            scale_y = saved_scale.y;
        }
    }
};

class wayfire_force_fullscreen;

std::map<wf::output_t*, wayfire_force_fullscreen*> wayfire_force_fullscreen_instances;

class wayfire_force_fullscreen : public wf::plugin_interface_t
{
    std::string transformer_name;
    std::unordered_multiset<fullscreen_transformer*> transformers;
    wf::option_wrapper_t<bool> preserve_aspect{"force-fullscreen/preserve_aspect"};
    wf::option_wrapper_t<wf::keybinding_t> key_toggle_fullscreen{"force-fullscreen/key_toggle_fullscreen"};

  public:
    void init() override
    {
        this->grab_interface->name = "force-fullscreen";
        this->grab_interface->capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR;
        transformer_name = this->grab_interface->name;

        output->add_key(key_toggle_fullscreen, &on_toggle_fullscreen);
        preserve_aspect.set_callback(preserve_aspect_option_changed);
        wayfire_force_fullscreen_instances[output] = this;
    }

    fullscreen_transformer *get_transformer(wf::point_t workspace)
    {
        for (auto t : transformers)
        {
            if (t->workspace == workspace)
            {
                return t;
            }
        }

        return nullptr;
    }

    fullscreen_transformer *get_transformer(wayfire_view view)
    {
        for (auto t : transformers)
        {
            if (t->view == view)
            {
                return t;
            }
        }

        return nullptr;
    }

    void setup_transform(fullscreen_transformer *transformer)
    {
        auto og = output->get_relative_geometry();

        transformer->setup_transform(og);
        transformer->view->damage();
    }

    wf::config::option_base_t::updated_callback_t preserve_aspect_option_changed = [=] ()
    {
        for (auto& t : transformers)
        {
            setup_transform(t);
        }
    };

    bool toggle_fullscreen(wayfire_view view)
    {
        if (!output->activate_plugin(grab_interface))
        {
            return false;
        }

        auto transformer = get_transformer(view);
        wlr_box saved_geometry;
        bool fullscreen;

        fullscreen = transformer ? false : true;

        wlr_box vg = view->get_output_geometry();

        if (fullscreen)
        {
            saved_geometry = vg;
        }

        view->set_fullscreen(fullscreen);

        if (fullscreen)
        {
            vg = view->get_wm_geometry();
            saved_geometry.width = vg.width;
            saved_geometry.height = vg.height;
        }
        else
        {
            deactivate(view);
            return true;
        }

        activate(view);

        transformer = get_transformer(view);
        transformer->fullscreen = true;
        transformer->saved_geometry = saved_geometry;

        return true;
    }

    wf::key_callback on_toggle_fullscreen = [=] (uint32_t key)
    {
        auto view = output->get_active_view();
        auto ws = output->workspace->get_current_workspace();
        auto transformer = get_transformer(ws);

        if (!view)
        {
            return false;
        }

        if (transformer && transformer->fullscreen && view != transformer->view)
        {
            return false;
        }

        return toggle_fullscreen(view);
    };

    void activate(wayfire_view view)
    {
        view->move(0, 0);
        fullscreen_transformer *transformer = new fullscreen_transformer(view);
        view->add_transformer(std::unique_ptr<fullscreen_transformer>(transformer), transformer_name);
        output->connect_signal("output-configuration-changed", &output_config_changed);
        wf::get_core().connect_signal("view-move-to-output", &view_output_changed);
        output->connect_signal("view-fullscreen-request", &view_fullscreened);
        view->connect_signal("geometry-changed", &view_geometry_changed);
        output->connect_signal("unmap-view", &view_unmapped);
        output->deactivate_plugin(grab_interface);
        transformers.insert(transformer);
        setup_transform(transformer);
    }

    void deactivate(wayfire_view view)
    {
        output->deactivate_plugin(grab_interface);
        auto transformer = get_transformer(view);

        if (!transformer)
        {
            return;
        }

        view_geometry_changed.disconnect();
        output_config_changed.disconnect();
        view_output_changed.disconnect();
        view_fullscreened.disconnect();
        view_unmapped.disconnect();
        view->move(transformer->saved_geometry.x, transformer->saved_geometry.y);
        transformers.erase(transformer);
        if (view->get_transformer(transformer_name))
        {
            view->pop_transformer(transformer_name);
        }
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

        for (auto& t : transformers)
        {
            setup_transform(t);
        }
    }};

    wf::signal_connection_t view_output_changed{[this] (wf::signal_data_t *data)
    {
        auto signal = static_cast<wf::view_move_to_output_signal*> (data);
        auto view = signal->view;
        auto transformer = get_transformer(view);

        if (!transformer)
        {
            return;
        }

        if (transformer->fullscreen)
        {
            toggle_fullscreen(view);
        }

        auto instance = wayfire_force_fullscreen_instances[signal->new_output];
        instance->toggle_fullscreen(view);
    }};

    wf::signal_connection_t view_unmapped{[this] (wf::signal_data_t *data)
    {
        auto view = get_signaled_view(data);
        auto transformer = get_transformer(view);

        if (!transformer)
        {
            return;
        }

        if (transformer->fullscreen)
        {
            toggle_fullscreen(view);
        }
    }};

    wf::signal_connection_t view_fullscreened{[this] (wf::signal_data_t *data)
    {
        auto signal = static_cast<view_fullscreen_signal*> (data);
        auto view = signal->view;
        auto transformer = get_transformer(view);

        if (!transformer)
        {
            return;
        }

        if (signal->state || signal->carried_out)
        {
            return;
        }

        if (transformer->fullscreen)
        {
            toggle_fullscreen(view);
        }

        signal->carried_out = true;
    }};

    wf::signal_connection_t view_geometry_changed{[this] (wf::signal_data_t *data)
    {
        auto view = get_signaled_view(data);
        auto transformer = get_transformer(view);

        if (!transformer)
        {
            return;
        }

        view->resize(transformer->saved_geometry.width, transformer->saved_geometry.height);
        setup_transform(transformer);
    }};

    void fini() override
    {
        output->rem_binding(&on_toggle_fullscreen);
        wayfire_force_fullscreen_instances.erase(output);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_force_fullscreen);
