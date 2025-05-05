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

/*
 * To set a workspace name, use the following option format:
 *
 * [workspace-names]
 * HDMI-A-1_workspace_3 = Foo
 *
 * This will show Foo when switching to workspace 3 on HDMI-A-1.
 * Enabling show_option_names will show all possible option names
 * on the respective workspaces and outputs.
 */

#include <map>
#include <wayfire/geometry.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/region.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/signal-provider.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/util.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-stream.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>
#include <wayfire/config/config-manager.hpp>

#define WIDGET_PADDING 20

struct workspace_name
{
    wf::geometry_t rect;
    std::string name;
    std::unique_ptr<wf::simple_texture_t> texture;
    cairo_t *cr = nullptr;
    cairo_surface_t *cairo_surface;
    cairo_text_extents_t text_extents;
};

namespace wf
{
namespace scene
{
namespace workspace_names
{
class simple_node_render_instance_t : public render_instance_t
{
    wf::signal::connection_t<node_damage_signal> on_node_damaged =
        [=] (node_damage_signal *ev)
    {
        push_to_parent(ev->region);
    };

    node_t *self;
    damage_callback push_to_parent;
    std::shared_ptr<workspace_name> workspace;
    wf::point_t *offset;
    double *alpha_fade;

  public:
    simple_node_render_instance_t(node_t *self, damage_callback push_dmg,
        wf::point_t *offset, double *alpha_fade,
        std::shared_ptr<workspace_name> workspace)
    {
        this->offset = offset;
        this->self   = self;
        this->workspace  = workspace;
        this->alpha_fade = alpha_fade;
        this->push_to_parent = push_dmg;
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
        wf::geometry_t g{workspace->rect.x + offset->x,
            workspace->rect.y + offset->y,
            workspace->rect.width, workspace->rect.height};
        OpenGL::render_begin(target);
        for (auto& box : region)
        {
            target.logic_scissor(wlr_box_from_pixman_box(box));
            OpenGL::render_texture(wf::texture_t{workspace->texture->tex},
                target, g, glm::vec4(1, 1, 1, *alpha_fade),
                OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
        }

        OpenGL::render_end();
    }
};


class simple_node_t : public node_t
{
    wf::point_t offset;
    double alpha_fade;

  public:
    std::shared_ptr<workspace_name> workspace;
    simple_node_t(wf::point_t offset) : node_t(false)
    {
        this->offset     = offset;
        this->alpha_fade = 0.0;
        workspace = std::make_shared<workspace_name>();
    }

    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override
    {
        // push_damage accepts damage in the parent's coordinate system
        // If the node is a transformer, it may transform the damage. However,
        // this simple nodes does not need any transformations, so the push_damage
        // callback is just passed along.
        instances.push_back(std::make_unique<simple_node_render_instance_t>(
            this, push_damage, &offset, &alpha_fade, workspace));
    }

    void do_push_damage(wf::region_t updated_region)
    {
        node_damage_signal ev;
        ev.region = updated_region;
        this->emit(&ev);
    }

    wf::geometry_t get_bounding_box() override
    {
        // Specify whatever geometry your node has
        return {workspace->rect.x + offset.x, workspace->rect.y + offset.y,
            workspace->rect.width, workspace->rect.height};
    }

    void set_offset(int x, int y)
    {
        this->offset.x = x;
        this->offset.y = y;
    }

    void set_alpha(double alpha)
    {
        this->alpha_fade = alpha;
    }
};

std::shared_ptr<simple_node_t> add_simple_node(wf::output_t *output,
    wf::point_t offset)
{
    auto subnode = std::make_shared<simple_node_t>(offset);
    wf::scene::add_front(output->node_for_layer(wf::scene::layer::OVERLAY), subnode);
    return subnode;
}

class wayfire_workspace_names_output : public wf::per_output_plugin_instance_t
{
    wf::wl_timer<false> timer;
    bool hook_set  = false;
    bool timed_out = false;
    std::vector<std::vector<std::shared_ptr<simple_node_t>>> workspaces;
    wf::option_wrapper_t<std::string> font{"workspace-names/font"};
    wf::option_wrapper_t<std::string> position{"workspace-names/position"};
    wf::option_wrapper_t<int> display_duration{"workspace-names/display_duration"};
    wf::option_wrapper_t<int> margin{"workspace-names/margin"};
    wf::option_wrapper_t<double> background_radius{
        "workspace-names/background_radius"};
    wf::option_wrapper_t<wf::color_t> text_color{"workspace-names/text_color"};
    wf::option_wrapper_t<wf::color_t> background_color{
        "workspace-names/background_color"};
    wf::option_wrapper_t<bool> show_option_names{"workspace-names/show_option_names"};
    wf::animation::simple_animation_t alpha_fade{display_duration};
    wf::option_wrapper_t<wf::config::compound_list_t<std::string>> workspace_names{"workspace-names/names"};

  public:
    void init() override
    {
        alpha_fade.set(0, 0);
        timed_out = false;

        auto wsize = output->wset()->get_workspace_grid_size();
        workspaces.resize(wsize.width);
        for (int x = 0; x < wsize.width; x++)
        {
            workspaces[x].resize(wsize.height);
        }

        auto og = output->get_relative_geometry();
        for (int x = 0; x < wsize.width; x++)
        {
            for (int y = 0; y < wsize.height; y++)
            {
                workspaces[x][y] = add_simple_node(output, {x *og.width,
                    y * og.height});
            }
        }

        output->connect(&workarea_changed);
        output->connect(&viewport_changed);
        font.set_callback(option_changed);
        position.set_callback(option_changed);
        background_color.set_callback(option_changed);
        text_color.set_callback(option_changed);
        show_option_names.set_callback(show_options_changed);

        if (show_option_names)
        {
            show_options_changed();
        } else
        {
            update_names();
        }

        wf::get_core().connect(&reload_config);
    }

    wf::signal::connection_t<wf::reload_config_signal> reload_config{[this] (wf::reload_config_signal *ev)
        {
            update_names();
        }
    };

    wf::config::option_base_t::updated_callback_t show_options_changed = [=] ()
    {
        update_names();

        viewport_changed.emit(nullptr);

        if (show_option_names)
        {
            timer.disconnect();
            output->render->rem_effect(&post_hook);
        } else
        {
            output->connect(&viewport_changed);
            output->render->add_effect(&post_hook, wf::OUTPUT_EFFECT_POST);
        }

        alpha_fade.animate(alpha_fade, 1.0);
        output->render->damage_whole();
    };

    void update_name(int x, int y)
    {
        auto section = wf::get_core().config->get_section("workspace-names");
        auto wsize   = output->wset()->get_workspace_grid_size();
        auto wsn     = workspaces[x][y]->workspace;
        int ws_num   = x + y * wsize.width + 1;

        // Get intended name of the workspace
        std::string tmp_name = output->to_string() + "_workspace_" + std::to_string(ws_num);

        if (show_option_names)
        {
            wsn->name = tmp_name;
        } else
        {
            bool option_found = false;
            for (const auto& [wsid, wsname] : workspace_names.value())
            {
                if (wsid == tmp_name)
                {
                    wsn->name    = wsname;
                    option_found = true;
                    break;
                }
            }

            if (!option_found)
            {
                wsn->name = "Workspace " + std::to_string(ws_num);
            }
        }
    }

    void update_names()
    {
        auto wsize = output->wset()->get_workspace_grid_size();
        for (int x = 0; x < wsize.width; x++)
        {
            for (int y = 0; y < wsize.height; y++)
            {
                update_name(x, y);
                update_texture(workspaces[x][y]->workspace);
            }
        }
    }

    void update_texture(std::shared_ptr<workspace_name> wsn)
    {
        update_texture_position(wsn);
        render_workspace_name(wsn);
    }

    void update_textures()
    {
        auto wsize = output->wset()->get_workspace_grid_size();
        for (int x = 0; x < wsize.width; x++)
        {
            for (int y = 0; y < wsize.height; y++)
            {
                update_texture(workspaces[x][y]->workspace);
            }
        }

        output->render->damage_whole();
    }

    void cairo_recreate(std::shared_ptr<workspace_name> wsn)
    {
        auto og = output->get_relative_geometry();
        auto font_size = og.height * 0.05;
        cairo_t *cr    = wsn->cr;
        cairo_surface_t *cairo_surface = wsn->cairo_surface;

        if (!cr)
        {
            /* Setup dummy context to get initial font size */
            cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
            cr = cairo_create(cairo_surface);
            wsn->texture = std::make_unique<wf::simple_texture_t>();
        }

        cairo_select_font_face(cr, std::string(
            font).c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, font_size);

        const char *name = wsn->name.c_str();
        cairo_text_extents(cr, name, &wsn->text_extents);

        wsn->rect.width  = wsn->text_extents.width + WIDGET_PADDING * 2;
        wsn->rect.height = wsn->text_extents.height + WIDGET_PADDING * 2;

        /* Recreate surface based on font size */
        cairo_destroy(cr);
        cairo_surface_destroy(cairo_surface);

        cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
            wsn->rect.width, wsn->rect.height);
        cr = cairo_create(cairo_surface);

        cairo_select_font_face(cr, std::string(
            font).c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, font_size);

        wsn->cr = cr;
        wsn->cairo_surface = cairo_surface;
    }

    wf::config::option_base_t::updated_callback_t option_changed = [=] ()
    {
        update_textures();
    };

    void update_texture_position(std::shared_ptr<workspace_name> wsn)
    {
        auto workarea = output->workarea->get_workarea();

        cairo_recreate(wsn);

        if ((std::string)position == "top_left")
        {
            wsn->rect.x = workarea.x + margin;
            wsn->rect.y = workarea.y + margin;
        } else if ((std::string)position == "top_center")
        {
            wsn->rect.x = workarea.x + (workarea.width / 2 - wsn->rect.width / 2);
            wsn->rect.y = workarea.y + margin;
        } else if ((std::string)position == "top_right")
        {
            wsn->rect.x = workarea.x + (workarea.width - wsn->rect.width) - margin;
            wsn->rect.y = workarea.y + margin;
        } else if ((std::string)position == "center_left")
        {
            wsn->rect.x = workarea.x + margin;
            wsn->rect.y = workarea.y + (workarea.height / 2 - wsn->rect.height / 2);
        } else if ((std::string)position == "center")
        {
            wsn->rect.x = workarea.x + (workarea.width / 2 - wsn->rect.width / 2);
            wsn->rect.y = workarea.y + (workarea.height / 2 - wsn->rect.height / 2);
        } else if ((std::string)position == "center_right")
        {
            wsn->rect.x = workarea.x + (workarea.width - wsn->rect.width) - margin;
            wsn->rect.y = workarea.y + (workarea.height / 2 - wsn->rect.height / 2);
        } else if ((std::string)position == "bottom_left")
        {
            wsn->rect.x = workarea.x + margin;
            wsn->rect.y = workarea.y + (workarea.height - wsn->rect.height) - margin;
        } else if ((std::string)position == "bottom_center")
        {
            wsn->rect.x = workarea.x + (workarea.width / 2 - wsn->rect.width / 2);
            wsn->rect.y = workarea.y + (workarea.height - wsn->rect.height) - margin;
        } else if ((std::string)position == "bottom_right")
        {
            wsn->rect.x = workarea.x + (workarea.width - wsn->rect.width) - margin;
            wsn->rect.y = workarea.y + (workarea.height - wsn->rect.height) - margin;
        } else
        {
            wsn->rect.x = workarea.x;
            wsn->rect.y = workarea.y;
        }
    }

    wf::signal::connection_t<wf::workarea_changed_signal> workarea_changed{[this] (wf::workarea_changed_signal
                                                                                   *ev)
        {
            update_textures();
        }
    };

    void cairo_clear(cairo_t *cr)
    {
        cairo_set_source_rgba(cr, 0, 0, 0, 0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
    }

    void render_workspace_name(std::shared_ptr<workspace_name> wsn)
    {
        double xc = wsn->rect.width / 2.0;
        double yc = wsn->rect.height / 2.0;
        int x2, y2;
        const char *name = wsn->name.c_str();
        double radius = background_radius;
        cairo_t *cr   = wsn->cr;

        cairo_clear(cr);

        x2 = wsn->rect.width;
        y2 = wsn->rect.height;

        cairo_set_source_rgba(cr,
            wf::color_t(background_color).r,
            wf::color_t(background_color).g,
            wf::color_t(background_color).b,
            wf::color_t(background_color).a);
        cairo_new_path(cr);
        cairo_arc(cr, radius, y2 - radius, radius, M_PI / 2, M_PI);
        cairo_line_to(cr, 0, radius);
        cairo_arc(cr, radius, radius, radius, M_PI, 3 * M_PI / 2);
        cairo_line_to(cr, x2 - radius, 0);
        cairo_arc(cr, x2 - radius, radius, radius, 3 * M_PI / 2, 2 * M_PI);
        cairo_line_to(cr, x2, y2 - radius);
        cairo_arc(cr, x2 - radius, y2 - radius, radius, 0, M_PI / 2);
        cairo_close_path(cr);
        cairo_fill(cr);

        cairo_set_source_rgba(cr,
            wf::color_t(text_color).r,
            wf::color_t(text_color).g,
            wf::color_t(text_color).b,
            wf::color_t(text_color).a);
        cairo_text_extents(cr, name, &wsn->text_extents);
        cairo_move_to(cr,
            xc - (wsn->text_extents.width / 2 + wsn->text_extents.x_bearing),
            yc - (wsn->text_extents.height / 2 + wsn->text_extents.y_bearing));
        cairo_show_text(cr, name);
        cairo_stroke(cr);

        OpenGL::render_begin();
        cairo_surface_upload_to_texture(wsn->cairo_surface, *wsn->texture);
        OpenGL::render_end();
    }

    void set_alpha()
    {
        auto wsize = output->wset()->get_workspace_grid_size();
        for (int x = 0; x < wsize.width; x++)
        {
            for (int y = 0; y < wsize.height; y++)
            {
                workspaces[x][y]->set_alpha(alpha_fade);
            }
        }
    }

    wf::effect_hook_t pre_hook = [=] ()
    {
        if (alpha_fade.running())
        {
            set_alpha();
            output->render->damage_whole();
        }
    };

    wf::signal::connection_t<wf::workspace_changed_signal> viewport_changed{[this] (wf::
                                                                                    workspace_changed_signal*
                                                                                    ev)
        {
            auto wsize = output->wset()->get_workspace_grid_size();
            auto nvp   = output->wset()->get_current_workspace();
            auto og    = output->get_relative_geometry();

            for (int x = 0; x < wsize.width; x++)
            {
                for (int y = 0; y < wsize.height; y++)
                {
                    workspaces[x][y]->set_offset((x - nvp.x) * og.width,
                        (y - nvp.y) * og.height);
                }
            }

            output->render->damage_whole();

            activate();

            if (show_option_names)
            {
                return;
            }

            if (!alpha_fade.running())
            {
                if (!timer.is_connected())
                {
                    alpha_fade.animate(alpha_fade, 1.0);
                }
            } else if (timed_out)
            {
                timed_out = false;
                alpha_fade.animate(alpha_fade, 1.0);
            }

            timer.disconnect();
            timer.set_timeout((int)display_duration, timeout);
        }
    };

    wf::wl_timer<false>::callback_t timeout = [=] ()
    {
        output->render->damage_whole();
        alpha_fade.animate(1.0, 0.0);
        timed_out = true;
    };

    wf::effect_hook_t post_hook = [=] ()
    {
        if (!alpha_fade.running())
        {
            if (timed_out)
            {
                deactivate();
                timed_out = false;
                output->render->damage_whole();
            } else if (!timer.is_connected())
            {
                timer.set_timeout((int)display_duration, timeout);
            }
        } else
        {
            set_alpha();
        }
    };

    void activate()
    {
        if (hook_set)
        {
            return;
        }

        output->render->add_effect(&post_hook, wf::OUTPUT_EFFECT_POST);
        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        output->render->damage_whole();
        hook_set = true;
    }

    void deactivate()
    {
        if (!hook_set)
        {
            return;
        }

        output->render->rem_effect(&post_hook);
        output->render->rem_effect(&pre_hook);
        hook_set = false;
    }

    void fini() override
    {
        deactivate();
        auto wsize = output->wset()->get_workspace_grid_size();
        for (int x = 0; x < wsize.width; x++)
        {
            for (int y = 0; y < wsize.height; y++)
            {
                auto& wsn = workspaces[x][y]->workspace;
                cairo_surface_destroy(wsn->cairo_surface);
                cairo_destroy(wsn->cr);
                wsn->texture->release();
                wsn->texture.reset();
                wf::scene::remove_child(workspaces[x][y]);
                workspaces[x][y].reset();
            }
        }

        output->render->damage_whole();
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_workspace_names_output>);
}
}
}
