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
#include <wayfire/util.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-stream.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>

#define WIDGET_PADDING 20

class workspace_name
{
  public:
    wf::geometry_t rect;
    std::string name;
    std::unique_ptr<wf::simple_texture_t> texture;
    cairo_t *cr = nullptr;
    cairo_surface_t *cairo_surface;
    cairo_text_extents_t text_extents;
};

class wayfire_workspace_names_screen : public wf::plugin_interface_t
{
    wf::wl_timer timer;
    bool hook_set  = false;
    bool timed_out = false;
    std::vector<std::vector<workspace_name>> workspaces;
    wf::option_wrapper_t<std::string> font{"workspace-names/font"};
    wf::option_wrapper_t<std::string> position{"workspace-names/position"};
    wf::option_wrapper_t<int> display_duration{"workspace-names/display_duration"};
    wf::option_wrapper_t<wf::color_t> text_color{"workspace-names/text_color"};
    wf::option_wrapper_t<wf::color_t> background_color{
        "workspace-names/background_color"};
    wf::option_wrapper_t<bool> show_option_names{"workspace-names/show_option_names"};
    wf::animation::simple_animation_t alpha_fade{display_duration};

  public:
    void init() override
    {
        grab_interface->name = "workspace-names";
        grab_interface->capabilities = 0;

        alpha_fade.set(0, 0);
        timed_out = false;

        auto wsize = output->workspace->get_workspace_grid_size();
        workspaces.resize(wsize.width);
        for (int x = 0; x < wsize.width; x++)
        {
            workspaces[x].resize(wsize.height);
        }

        output->connect_signal("workarea-changed", &workarea_changed);
        output->connect_signal("workspace-changed", &viewport_changed);
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

        wf::get_core().connect_signal("reload-config", &reload_config);
    }

    wf::signal_connection_t reload_config{[this] (wf::signal_data_t *data)
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
            viewport_changed.disconnect();
            output->render->rem_effect(&post_hook);
        } else
        {
            output->connect_signal("workspace-changed", &viewport_changed);
            output->render->add_effect(&post_hook, wf::OUTPUT_EFFECT_POST);
        }

        alpha_fade.animate(alpha_fade, 1.0);
        output->render->damage_whole();
    };

    void update_name(int x, int y)
    {
        auto section = wf::get_core().config.get_section(grab_interface->name);
        auto wsize   = output->workspace->get_workspace_grid_size();
        workspace_name& wsn = workspaces[x][y];
        int ws_num = x + y * wsize.width + 1;

        if (show_option_names)
        {
            wsn.name = output->to_string() + "_workspace_" +
                std::to_string(ws_num);
        } else
        {
            bool option_found = false;
            for (auto option : section->get_registered_options())
            {
                int ws;
                if (sscanf(option->get_name().c_str(),
                    (output->to_string() + "_workspace_%d").c_str(), &ws) == 1)
                {
                    if (ws == ws_num)
                    {
                        wsn.name     = option->get_value_str();
                        option_found = true;
                        break;
                    }
                }
            }

            if (!option_found)
            {
                wsn.name = "Workspace " + std::to_string(ws_num);
            }
        }
    }

    void update_names()
    {
        auto wsize = output->workspace->get_workspace_grid_size();
        for (int x = 0; x < wsize.width; x++)
        {
            for (int y = 0; y < wsize.height; y++)
            {
                update_name(x, y);
                update_texture(workspaces[x][y]);
            }
        }
    }

    void update_texture(workspace_name& wsn)
    {
        update_texture_position(wsn);
        render_workspace_name(wsn);
    }

    void update_textures()
    {
        auto wsize = output->workspace->get_workspace_grid_size();
        for (int x = 0; x < wsize.width; x++)
        {
            for (int y = 0; y < wsize.height; y++)
            {
                update_texture(workspaces[x][y]);
            }
        }

        output->render->damage_whole();
    }

    void cairo_recreate(workspace_name& wsn)
    {
        auto og = output->get_relative_geometry();
        auto font_size = og.height * 0.05;
        cairo_t *cr    = wsn.cr;
        cairo_surface_t *cairo_surface = wsn.cairo_surface;

        if (!cr)
        {
            /* Setup dummy context to get initial font size */
            cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
            cr = cairo_create(cairo_surface);
            wsn.texture = std::make_unique<wf::simple_texture_t>();
        }

        cairo_select_font_face(cr, std::string(
            font).c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, font_size);

        const char *name = wsn.name.c_str();
        cairo_text_extents(cr, name, &wsn.text_extents);

        wsn.rect.width  = wsn.text_extents.width + WIDGET_PADDING * 2;
        wsn.rect.height = wsn.text_extents.height + WIDGET_PADDING * 2;

        /* Recreate surface based on font size */
        cairo_destroy(cr);
        cairo_surface_destroy(cairo_surface);

        cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
            wsn.rect.width, wsn.rect.height);
        cr = cairo_create(cairo_surface);

        cairo_select_font_face(cr, std::string(
            font).c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, font_size);

        wsn.cr = cr;
        wsn.cairo_surface = cairo_surface;
    }

    wf::config::option_base_t::updated_callback_t option_changed = [=] ()
    {
        update_textures();
    };

    void update_texture_position(workspace_name& wsn)
    {
        auto workarea = output->workspace->get_workarea();

        cairo_recreate(wsn);

        if ((std::string)position == "top_left")
        {
            wsn.rect.x = workarea.x;
            wsn.rect.y = workarea.y;
        } else if ((std::string)position == "top_center")
        {
            wsn.rect.x = workarea.x + (workarea.width / 2 - wsn.rect.width / 2);
            wsn.rect.y = workarea.y;
        } else if ((std::string)position == "top_right")
        {
            wsn.rect.x = workarea.x + (workarea.width - wsn.rect.width);
            wsn.rect.y = workarea.y;
        } else if ((std::string)position == "center_left")
        {
            wsn.rect.x = workarea.x;
            wsn.rect.y = workarea.y + (workarea.height / 2 - wsn.rect.height / 2);
        } else if ((std::string)position == "center")
        {
            wsn.rect.x = workarea.x + (workarea.width / 2 - wsn.rect.width / 2);
            wsn.rect.y = workarea.y + (workarea.height / 2 - wsn.rect.height / 2);
        } else if ((std::string)position == "center_right")
        {
            wsn.rect.x = workarea.x + (workarea.width - wsn.rect.width);
            wsn.rect.y = workarea.y + (workarea.height / 2 - wsn.rect.height / 2);
        } else if ((std::string)position == "bottom_left")
        {
            wsn.rect.x = workarea.x;
            wsn.rect.y = workarea.y + (workarea.height - wsn.rect.height);
        } else if ((std::string)position == "bottom_center")
        {
            wsn.rect.x = workarea.x + (workarea.width / 2 - wsn.rect.width / 2);
            wsn.rect.y = workarea.y + (workarea.height - wsn.rect.height);
        } else if ((std::string)position == "bottom_right")
        {
            wsn.rect.x = workarea.x + (workarea.width - wsn.rect.width);
            wsn.rect.y = workarea.y + (workarea.height - wsn.rect.height);
        } else
        {
            wsn.rect.x = workarea.x;
            wsn.rect.y = workarea.y;
        }
    }

    wf::signal_connection_t workarea_changed{[this] (wf::signal_data_t *data)
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

    /* GLESv2 doesn't support GL_BGRA */
    void cairo_set_source_rgba_swizzle(cairo_t *cr, double r, double g, double b,
        double a)
    {
        cairo_set_source_rgba(cr, b, g, r, a);
    }

    void render_workspace_name(workspace_name& wsn)
    {
        double xc = wsn.rect.width / 2;
        double yc = wsn.rect.height / 2;
        int x2, y2;
        const char *name = wsn.name.c_str();
        double radius = 30;
        cairo_t *cr   = wsn.cr;

        cairo_clear(cr);

        x2 = wsn.rect.width;
        y2 = wsn.rect.height;

        cairo_set_source_rgba_swizzle(cr,
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

        cairo_set_source_rgba_swizzle(cr,
            wf::color_t(text_color).r,
            wf::color_t(text_color).g,
            wf::color_t(text_color).b,
            wf::color_t(text_color).a);
        cairo_text_extents(cr, name, &wsn.text_extents);
        cairo_move_to(cr,
            xc - (wsn.text_extents.width / 2 + wsn.text_extents.x_bearing),
            yc - (wsn.text_extents.height / 2 + wsn.text_extents.y_bearing));
        cairo_show_text(cr, name);
        cairo_stroke(cr);

        OpenGL::render_begin();
        cairo_surface_upload_to_texture(wsn.cairo_surface, *wsn.texture);
        OpenGL::render_end();
    }

    wf::effect_hook_t pre_hook = [=] ()
    {
        if (alpha_fade.running())
        {
            output->render->damage_whole();
        }
    };

    wf::signal_connection_t viewport_changed{[this] (wf::signal_data_t *data)
        {
            activate();

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

            if (timer.is_connected())
            {
                timer.disconnect();
                timer.set_timeout((int)display_duration, timeout);
            }
        }
    };

    wf::wl_timer::callback_t timeout = [=] ()
    {
        output->render->damage_whole();
        alpha_fade.animate(1.0, 0.0);
        timer.disconnect();
        timed_out = true;
    };

    wf::signal_connection_t workspace_stream_post{[this] (wf::signal_data_t *data)
        {
            const auto& workspace = static_cast<wf::stream_signal_t*>(data);
            auto& wsn   = workspaces[workspace->ws.x][workspace->ws.y];
            auto damage = output->render->get_scheduled_damage() &
                output->render->get_ws_box(workspace->ws);
            auto og   = workspace->fb.geometry;
            auto rect = wsn.rect;

            rect.x += og.x;
            rect.y += og.y;

            OpenGL::render_begin(workspace->fb);
            for (auto& box : damage)
            {
                workspace->fb.logic_scissor(wlr_box_from_pixman_box(box));
                OpenGL::render_texture(wf::texture_t{wsn.texture->tex},
                    workspace->fb, rect, glm::vec4(1, 1, 1, alpha_fade),
                    OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
            }

            OpenGL::render_end();
        }
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
        }
    };

    void activate()
    {
        if (hook_set)
        {
            return;
        }

        output->render->connect_signal("workspace-stream-post",
            &workspace_stream_post);
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
        workspace_stream_post.disconnect();
        hook_set = false;
    }

    void fini() override
    {
        deactivate();
        auto wsize = output->workspace->get_workspace_grid_size();
        for (int x = 0; x < wsize.width; x++)
        {
            for (int y = 0; y < wsize.height; y++)
            {
                auto& wsn = workspaces[x][y];
                cairo_surface_destroy(wsn.cairo_surface);
                cairo_destroy(wsn.cr);
                wsn.texture->release();
                wsn.texture.reset();
            }
        }

        output->render->damage_whole();
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_workspace_names_screen);
