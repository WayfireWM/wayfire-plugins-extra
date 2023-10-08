/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Ilia Bozhinov
 * Copyright (c) 2023 Scott Moreau
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
#include <wayfire/core.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/compositor-view.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/txn/transaction-manager.hpp>

namespace wf
{
namespace scene
{
namespace force_fullscreen
{
class simple_node_render_instance_t : public render_instance_t
{
    wf::signal::connection_t<node_damage_signal> on_node_damaged =
        [=] (node_damage_signal *ev)
    {
        push_to_parent(ev->region);
    };

    node_t *self;
    wayfire_toplevel_view view;
    damage_callback push_to_parent;
    int *x, *y, *w, *h;
    wlr_box *transparent_box;
    wf::option_wrapper_t<bool> transparent_behind_views{
        "force-fullscreen/transparent_behind_views"};

  public:
    simple_node_render_instance_t(node_t *self, damage_callback push_damage,
        wayfire_toplevel_view view, int *x, int *y, int *w, int *h, wlr_box *transparent_box)
    {
        this->x    = x;
        this->y    = y;
        this->w    = w;
        this->h    = h;
        this->self = self;
        this->view = view;
        this->transparent_box = transparent_box;
        this->push_to_parent  = push_damage;
        self->connect(&on_node_damaged);
    }

    void schedule_instructions(
        std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage)
    {
        // We want to render ourselves only, the node does not have children
        instructions.push_back(render_instruction_t{
                        .instance = this,
                        .target   = target,
                        .damage   = damage & self->get_bounding_box(),
                    });
    }

    void render(const wf::render_target_t& target,
        const wf::region_t& region)
    {
        auto output = view->get_output();

        if (!output)
        {
            return;
        }

        wf::region_t scissor_region{region};
        if (transparent_behind_views)
        {
            auto bbox = *transparent_box;
            bbox.x     += 1;
            bbox.y     += 1;
            bbox.width -= 2;
            bbox.height    -= 2;
            scissor_region ^= wf::region_t{bbox};
        }

        OpenGL::render_begin(target);
        for (auto& box : scissor_region)
        {
            target.logic_scissor(wlr_box_from_pixman_box(box));
            OpenGL::clear({0, 0, 0, 1});
        }

        OpenGL::render_end();
    }
};

class black_border_node_t : public node_t
{
    wayfire_toplevel_view view;
    wlr_box transparent_box;

  public:
    int x, y, w, h;

    black_border_node_t(wayfire_toplevel_view view, int x, int y, int w,
        int h, wlr_box transparent_box) : node_t(false)
    {
        this->x    = x;
        this->y    = y;
        this->w    = w;
        this->h    = h;
        this->view = view;
        this->transparent_box = transparent_box;
    }

    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override
    {
        // push_damage accepts damage in the parent's coordinate system
        // If the node is a transformer, it may transform the damage. However,
        // this simple nodes does not need any transformations, so the push_damage
        // callback is just passed along.
        instances.push_back(std::make_unique<simple_node_render_instance_t>(
            this, push_damage, view, &x, &y, &w, &h, &transparent_box));
    }

    wf::geometry_t get_bounding_box() override
    {
        // Specify whatever geometry your node has
        return {x, y, w, h};
    }
};

class fullscreen_background
{
  public:
    wf::geometry_t saved_geometry;
    wf::geometry_t undecorated_geometry;
    std::shared_ptr<wf::scene::view_2d_transformer_t> transformer;
    std::shared_ptr<black_border_node_t> black_border_node;
    bool black_border = false;
    wlr_box transformed_view_box;

    fullscreen_background(wayfire_toplevel_view view)
    {}

    ~fullscreen_background()
    {}
};

class wayfire_force_fullscreen;

std::map<wf::output_t*,
    wayfire_force_fullscreen*> wayfire_force_fullscreen_instances;

class wayfire_force_fullscreen : public wf::per_output_plugin_instance_t
{
    std::string background_name;
    bool motion_connected = false;
    std::map<wayfire_toplevel_view, std::unique_ptr<fullscreen_background>> backgrounds;
    wf::option_wrapper_t<bool> preserve_aspect{"force-fullscreen/preserve_aspect"};
    wf::option_wrapper_t<bool> constrain_pointer{"force-fullscreen/constrain_pointer"};
    wf::option_wrapper_t<std::string> constraint_area{
        "force-fullscreen/constraint_area"};
    wf::option_wrapper_t<bool> transparent_behind_views{
        "force-fullscreen/transparent_behind_views"};
    wf::option_wrapper_t<wf::keybinding_t> key_toggle_fullscreen{
        "force-fullscreen/key_toggle_fullscreen"};
    wf::plugin_activation_data_t grab_interface{
        .name = "force-fullscreen",
        .capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR,
    };

  public:
    void init() override
    {
        background_name = this->grab_interface.name;

        output->add_key(key_toggle_fullscreen, &on_toggle_fullscreen);
        transparent_behind_views.set_callback(option_changed);
        wayfire_force_fullscreen_instances[output] = this;
        constrain_pointer.set_callback(constrain_pointer_option_changed);
        preserve_aspect.set_callback(option_changed);
        output->connect(&viewport_changed);
    }

    wf::signal::connection_t<wf::workspace_changed_signal> viewport_changed{[this] (wf::
                                                                                    workspace_changed_signal*
                                                                                    ev)
        {
            auto og  = output->get_relative_geometry();
            auto nvp = ev->new_viewport;

            for (auto& b : backgrounds)
            {
                int w   = (og.width - b.second->transformed_view_box.width) / 2.0f;
                auto ws = output->wset()->get_view_main_workspace(b.first);
                auto offset = ws - nvp;
                int x = offset.x * og.width;
                int y = offset.y * og.height;
                b.second->transformed_view_box.x = x + w;
                b.second->black_border_node->x   = x;
                b.second->black_border_node->y   = y;
                b.second->black_border_node->w   = og.width;
                b.second->black_border_node->h   = og.height;
                b.second->transformed_view_box.y = b.second->black_border_node->y =
                    y;
            }

            output->render->damage_whole();
        }
    };

    void ensure_subsurface(wayfire_toplevel_view view, wlr_box transformed_view_box)
    {
        auto pair = backgrounds.find(view);

        if (pair == backgrounds.end())
        {
            return;
        }

        auto& background = pair->second;

        if (!background->black_border)
        {
            auto output = view->get_output();
            if (!output)
            {
                return;
            }

            auto og = output->get_relative_geometry();
            background->black_border_node = std::make_shared<black_border_node_t>(
                view, 0, 0, og.width, og.height, transformed_view_box);
            wf::scene::add_back(view->get_root_node(),
                background->black_border_node);
            background->black_border = true;
        }
    }

    void destroy_subsurface(wayfire_toplevel_view view)
    {
        auto pair = backgrounds.find(view);

        if (pair == backgrounds.end())
        {
            return;
        }

        auto& background = pair->second;

        if (background->black_border)
        {
            wf::scene::remove_child(background->black_border_node);
            background->black_border = false;
        }
    }

    void setup_transform(wayfire_toplevel_view view)
    {
        auto og = output->get_relative_geometry();
        auto vg = view->get_geometry();

        double scale_x = (double)og.width / vg.width;
        double scale_y = (double)og.height / vg.height;
        double translation_x = (og.width - vg.width) / 2.0;
        double translation_y = (og.height - vg.height) / 2.0;

        if (preserve_aspect)
        {
            scale_x = scale_y = std::min(scale_x, scale_y);
        }

        wlr_box box;
        box.width  = std::floor(vg.width * scale_x);
        box.height = std::floor(vg.height * scale_y);
        box.x = std::ceil((og.width - box.width) / 2.0);
        box.y = std::ceil((og.height - box.height) / 2.0);

        destroy_subsurface(view);
        if (!transparent_behind_views || preserve_aspect)
        {
            ensure_subsurface(view, box);
        }

        if (preserve_aspect)
        {
            scale_x += 1.0 / vg.width;
            translation_x -= 1.0;
        }

        backgrounds[view]->transformed_view_box = box;
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
            destroy_subsurface(b.first);
            setup_transform(b.first);
        }
    }

    bool toggle_fullscreen(wayfire_toplevel_view view)
    {
        if (!output->can_activate_plugin(&grab_interface))
        {
            return false;
        }

        wlr_box saved_geometry = view->get_geometry();

        auto background = backgrounds.find(view);
        bool fullscreen = background == backgrounds.end() ? true : false;

        view->toplevel()->pending().fullscreen = fullscreen;
        wf::get_core().tx_manager->schedule_object(view->toplevel());

        wlr_box undecorated_geometry = view->get_geometry();

        if (!fullscreen)
        {
            deactivate(view);

            return true;
        }

        activate(view);

        background = backgrounds.find(view);
        if (background == backgrounds.end())
        {
            /* Should never happen */
            deactivate(view);

            return true;
        }

        background->second->undecorated_geometry = undecorated_geometry;
        background->second->saved_geometry = saved_geometry;

        setup_transform(view);

        return true;
    }

    wf::key_callback on_toggle_fullscreen = [=] (auto)
    {
        auto view = wf::toplevel_cast(wf::get_active_view_for_output(output));

        if (!view || (view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT))
        {
            return false;
        }

        return toggle_fullscreen(view);
    };

    void activate(wayfire_toplevel_view view)
    {
        view->move(0, 0);
        backgrounds[view] = std::make_unique<fullscreen_background>(view);
        backgrounds[view]->transformer =
            std::make_shared<wf::scene::view_2d_transformer_t>(view);
        view->get_transformed_node()->add_transformer(backgrounds[view]->transformer,
            wf::TRANSFORMER_2D,
            background_name);
        output->connect(&output_config_changed);
        wf::get_core().connect(&view_output_changed);
        output->connect(&view_fullscreened);
        view->connect(&view_geometry_changed);
        output->connect(&view_unmapped);
        output->connect(&view_focused);
        if (constrain_pointer)
        {
            connect_motion_signal();
        }
    }

    void deactivate(wayfire_toplevel_view view)
    {
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
            disconnect_motion_signal();
            view_focused.disconnect();
        }

        view->move(
            background->second->saved_geometry.x,
            background->second->saved_geometry.y);

        if (view->get_transformed_node()->get_transformer(background_name))
        {
            view->get_transformed_node()->rem_transformer(
                background->second->transformer);
        }

        destroy_subsurface(view);
        backgrounds.erase(view);
    }

    void connect_motion_signal()
    {
        if (motion_connected)
        {
            return;
        }

        wf::get_core().connect(&on_motion_event);
        motion_connected = true;
    }

    void disconnect_motion_signal()
    {
        if (!motion_connected)
        {
            return;
        }

        on_motion_event.disconnect();
        motion_connected = false;
    }

    void update_motion_signal(wayfire_toplevel_view view)
    {
        if (view && (view->get_output() == output) && constrain_pointer &&
            (backgrounds.find(view) != backgrounds.end()))
        {
            connect_motion_signal();

            return;
        }

        disconnect_motion_signal();
    }

    wf::config::option_base_t::updated_callback_t constrain_pointer_option_changed =
        [=] ()
    {
        auto view = wf::toplevel_cast(wf::get_active_view_for_output(output));
        update_motion_signal(view);
    };

    wf::config::option_base_t::updated_callback_t option_changed = [=] ()
    {
        update_backgrounds();
    };

    wf::signal::connection_t<wf::input_event_signal<wlr_pointer_motion_event>> on_motion_event =
        [=] (wf::input_event_signal<wlr_pointer_motion_event> *ev)
    {
        if (wf::get_core().seat->get_active_output() != output)
        {
            return;
        }

        if (!output->can_activate_plugin(&grab_interface))
        {
            return;
        }

        auto cursor = wf::get_core().get_cursor_position();
        auto last_cursor = cursor;
        auto og = output->get_layout_geometry();

        cursor.x += ev->event->delta_x;
        cursor.y += ev->event->delta_y;

        for (auto& b : backgrounds)
        {
            auto view = wf::get_active_view_for_output(output);
            wlr_box box;

            box    = b.second->transformed_view_box;
            box.x += og.x;
            box.y += og.y;

            if (std::string(constraint_area) == "output")
            {
                box = og;
            }

            if ((b.first == view) &&
                !(box & wf::pointf_t{cursor.x, cursor.y}))
            {
                wlr_box_closest_point(&box, cursor.x, cursor.y,
                    &cursor.x, &cursor.y);
                ev->event->delta_x = ev->event->unaccel_dx =
                    cursor.x - last_cursor.x;
                ev->event->delta_y = ev->event->unaccel_dy =
                    cursor.y - last_cursor.y;

                return;
            }
        }
    };

    wf::signal::connection_t<wf::output_configuration_changed_signal> output_config_changed{[this] (wf::
                                                                                                    output_configuration_changed_signal
                                                                                                    *ev)
        {
            if (!ev->changed_fields)
            {
                return;
            }

            if (ev->changed_fields & wf::OUTPUT_SOURCE_CHANGE)
            {
                return;
            }

            update_backgrounds();
        }
    };

    wf::signal::connection_t<wf::view_pre_moved_to_wset_signal> view_output_changed{[this] (wf::
                                                                                            view_pre_moved_to_wset_signal
                                                                                            *ev)
        {
            auto view = ev->view;
            auto background = backgrounds.find(view);

            if (background == backgrounds.end())
            {
                return;
            }

            if (!ev->new_wset->get_attached_output())
            {
                return;
            }

            toggle_fullscreen(view);

            auto instance = wayfire_force_fullscreen_instances[ev->new_wset->get_attached_output()];
            instance->toggle_fullscreen(view);
        }
    };

    wf::signal::connection_t<wf::view_focus_request_signal> view_focused{[this] (wf::view_focus_request_signal
                                                                                 *ev)
        {
            auto view = toplevel_cast(ev->view);
            update_motion_signal(view);
        }
    };

    wf::signal::connection_t<wf::view_unmapped_signal> view_unmapped{[this] (wf::view_unmapped_signal *ev)
        {
            auto view = toplevel_cast(ev->view);
            auto background = backgrounds.find(view);

            if (background == backgrounds.end())
            {
                return;
            }

            toggle_fullscreen(view);
        }
    };

    wf::signal::connection_t<wf::view_fullscreen_request_signal> view_fullscreened{[this] (wf::
                                                                                           view_fullscreen_request_signal
                                                                                           *ev)
        {
            auto view = ev->view;
            auto background = backgrounds.find(view);

            if (background == backgrounds.end())
            {
                return;
            }

            if (ev->state || ev->carried_out)
            {
                return;
            }

            toggle_fullscreen(view);

            ev->carried_out = true;
        }
    };

    wf::signal::connection_t<wf::view_geometry_changed_signal> view_geometry_changed{[this] (wf::
                                                                                             view_geometry_changed_signal
                                                                                             *ev)
        {
            auto view = ev->view;
            auto background = backgrounds.find(view);

            if (background == backgrounds.end())
            {
                return;
            }

            view->resize(
                background->second->undecorated_geometry.width,
                background->second->undecorated_geometry.height);
            setup_transform(view);
        }
    };

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

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_force_fullscreen>);
}
}
}
