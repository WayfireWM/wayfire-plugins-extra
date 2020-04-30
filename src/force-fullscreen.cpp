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
#include <wayfire/compositor-view.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>

class fullscreen_subsurface : public wf::surface_interface_t, public wf::compositor_surface_t
{
  public:
    bool _mapped = true;
    wf::geometry_t geometry;

    fullscreen_subsurface(wayfire_view view)
        : wf::surface_interface_t(), wf::compositor_surface_t() {}

    ~fullscreen_subsurface()
    {
        _mapped = false;
        wf::emit_map_state_change(this);
    }

    bool accepts_input(int32_t sx, int32_t sy) override
    {
        if (sx < geometry.x || sx > geometry.x + geometry.width ||
            sy < geometry.y || sy > geometry.y + geometry.height)
            return false;

        return true;
    }

    bool is_mapped() const override
    {
        return _mapped;
    }

    wf::point_t get_offset() override
    {
        return {geometry.x, geometry.y};
    }

    wf::dimensions_t get_size() const override
    {
        return {geometry.width, geometry.height};
    }

    void simple_render(const wf::framebuffer_t& fb, int x, int y,
        const wf::region_t& damage) override
    {
        OpenGL::render_begin(fb);
        for (const auto& box : damage)
        {
            fb.logic_scissor(wlr_box_from_pixman_box(box));
            OpenGL::clear({0, 0, 0, 1});
        }
        OpenGL::render_end();
    }
};

class fullscreen_background
{
  public:
    wf::view_2D *transformer;
    wf::geometry_t saved_geometry;
    fullscreen_subsurface *subsurface_a = nullptr;
    fullscreen_subsurface *subsurface_b = nullptr;

    fullscreen_background(wayfire_view view) {}

    ~fullscreen_background() {}
};

class wayfire_force_fullscreen;

std::map<wf::output_t*, wayfire_force_fullscreen*> wayfire_force_fullscreen_instances;

class wayfire_force_fullscreen : public wf::plugin_interface_t
{
    std::string background_name;
    std::map<wayfire_view, std::unique_ptr<fullscreen_background>> backgrounds;
    wf::option_wrapper_t<bool> preserve_aspect{"force-fullscreen/preserve_aspect"};
    wf::option_wrapper_t<wf::keybinding_t> key_toggle_fullscreen{"force-fullscreen/key_toggle_fullscreen"};

  public:
    void init() override
    {
        this->grab_interface->name = "force-fullscreen";
        this->grab_interface->capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR;
        background_name = this->grab_interface->name;

        output->add_key(key_toggle_fullscreen, &on_toggle_fullscreen);
        preserve_aspect.set_callback(preserve_aspect_option_changed);
        wayfire_force_fullscreen_instances[output] = this;
    }

    void create_subsurfaces(wayfire_view view)
    {
        auto pair = backgrounds.find(view);

        if (pair == backgrounds.end())
        {
            return;
        }

        auto& background = pair->second;

        if (!background->subsurface_a)
        {
            std::unique_ptr<fullscreen_subsurface> subsurface_a = std::make_unique<fullscreen_subsurface>(view);
            nonstd::observer_ptr<fullscreen_subsurface> ptr{subsurface_a};
            view->add_subsurface(std::move(subsurface_a), true);
            background->subsurface_a = ptr.get();
        }
        if (!background->subsurface_b)
        {
            std::unique_ptr<fullscreen_subsurface> subsurface_b = std::make_unique<fullscreen_subsurface>(view);
            nonstd::observer_ptr<fullscreen_subsurface> ptr{subsurface_b};
            view->add_subsurface(std::move(subsurface_b), true);
            background->subsurface_b = ptr.get();
        }
    }

    void destroy_subsurfaces(wayfire_view view)
    {
        auto pair = backgrounds.find(view);

        if (pair == backgrounds.end())
        {
            return;
        }

        auto& background = pair->second;

        if (background->subsurface_a)
        {
            view->remove_subsurface(background->subsurface_a);
            background->subsurface_a = nullptr;
        }
        if (background->subsurface_b)
        {
            view->remove_subsurface(background->subsurface_b);
            background->subsurface_b = nullptr;
        }
    }

    void setup_transform(wayfire_view view)
    {
        auto og = output->get_relative_geometry();
        auto vg = view->get_wm_geometry();
        wf::geometry_t subsurface_a;
        wf::geometry_t subsurface_b;

        double scale_x = (double) og.width / vg.width;
        double scale_y = (double) og.height / vg.height;
        double translation_x = (og.width - vg.width) / 2.0;
        double translation_y = (og.height - vg.height) / 2.0;

        if (preserve_aspect)
        {
            if (scale_x > scale_y)
            {
                scale_x = scale_y;
                subsurface_a.width = ((og.width * (1.0 / scale_x)) - vg.width) / 2.0 + 1;
                subsurface_a.height = vg.height;
                subsurface_a.x = -subsurface_a.width;
                subsurface_a.y = 0;
                subsurface_b = subsurface_a;
                subsurface_b.x = vg.width;
                subsurface_b.y = 0;
            }
            else if (scale_x < scale_y)
            {
                scale_y = scale_x;
                subsurface_a.width = vg.width;
                subsurface_a.height = ((og.height * (1.0 / scale_y)) - vg.height) / 2.0 + 1;
                subsurface_a.x = 0;
                subsurface_a.y = -subsurface_a.height;
                subsurface_b = subsurface_a;
                subsurface_b.x = 0;
                subsurface_b.y = vg.height;
            }

            create_subsurfaces(view);
            backgrounds[view]->subsurface_a->geometry = subsurface_a;
            backgrounds[view]->subsurface_b->geometry = subsurface_b;
        }

        backgrounds[view]->transformer->scale_x = scale_x;
        backgrounds[view]->transformer->scale_y = scale_y;
        backgrounds[view]->transformer->translation_x = translation_x;
        backgrounds[view]->transformer->translation_y = translation_y;

        view->damage();
    }

    void update_backgrounds()
    {
	for (auto& b : backgrounds)
	{
            destroy_subsurfaces(b.first);
            setup_transform(b.first);
	}
    }

    wf::config::option_base_t::updated_callback_t preserve_aspect_option_changed = [=] ()
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

        if (!fullscreen)
        {
            deactivate(view);
            return true;
        }

        activate(view);

        background = backgrounds.find(view);
        if (background != backgrounds.end())
        {
            background->second->saved_geometry = saved_geometry;
        }

        return true;
    }

    wf::key_callback on_toggle_fullscreen = [=] (uint32_t key)
    {
        auto view = output->get_active_view();
        auto background = backgrounds.find(view);

        if (!view)
        {
            return false;
        }

        if (background != backgrounds.end() && view != background->first)
        {
            return false;
        }

        return toggle_fullscreen(view);
    };

    void activate(wayfire_view view)
    {
        view->move(0, 0);
        backgrounds[view] = std::make_unique<fullscreen_background>(view);
        backgrounds[view]->transformer = new wf::view_2D(view);
        view->add_transformer(std::unique_ptr<wf::view_2D>(backgrounds[view]->transformer), background_name);
        output->connect_signal("output-configuration-changed", &output_config_changed);
        wf::get_core().connect_signal("view-move-to-output", &view_output_changed);
        output->connect_signal("view-fullscreen-request", &view_fullscreened);
        view->connect_signal("geometry-changed", &view_geometry_changed);
        output->connect_signal("unmap-view", &view_unmapped);
        output->deactivate_plugin(grab_interface);
        setup_transform(view);
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
        view->move(background->second->saved_geometry.x, background->second->saved_geometry.y);
        if (view->get_transformer(background_name))
        {
            view->pop_transformer(background_name);
        }
        destroy_subsurfaces(view);
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

        view->resize(background->second->saved_geometry.width, background->second->saved_geometry.height);
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
