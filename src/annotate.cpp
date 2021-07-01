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
#include <math.h>
#include <wayfire/util.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-stream.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>

enum annotate_draw_method
{
    ANNOTATE_METHOD_DRAW,
    ANNOTATE_METHOD_LINE,
    ANNOTATE_METHOD_RECTANGLE,
    ANNOTATE_METHOD_CIRCLE,
};

class anno_ws_overlay
{
  public:
    cairo_t *cr = nullptr;
    cairo_surface_t *cairo_surface;
    std::unique_ptr<wf::simple_texture_t> texture;
};

class wayfire_annotate_screen : public wf::plugin_interface_t
{
    uint32_t button;
    wlr_box last_bbox;
    bool hook_set = false;
    anno_ws_overlay shape_overlay;
    annotate_draw_method draw_method;
    wf::pointf_t grab_point, last_cursor;
    std::vector<std::vector<anno_ws_overlay>> overlays;
    wf::option_wrapper_t<std::string> method{"annotate/method"};
    wf::option_wrapper_t<double> line_width{"annotate/line_width"};
    wf::option_wrapper_t<bool> shapes_from_center{"annotate/from_center"};
    wf::option_wrapper_t<wf::color_t> stroke_color{"annotate/stroke_color"};
    wf::option_wrapper_t<wf::buttonbinding_t> draw_binding{"annotate/draw"};
    wf::option_wrapper_t<wf::activatorbinding_t> clear_binding{
        "annotate/clear_workspace"};

  public:
    void init() override
    {
        grab_interface->name = "annotate";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR;

        auto wsize = output->workspace->get_workspace_grid_size();
        overlays.resize(wsize.width);
        for (int x = 0; x < wsize.width; x++)
        {
            overlays[x].resize(wsize.height);
        }

        grab_interface->callbacks.pointer.button = [=] (uint32_t b, uint32_t s)
        {
            if ((b == button) && (s == WL_POINTER_BUTTON_STATE_RELEASED))
            {
                draw_end();
            }
        };

        output->connect_signal("output-configuration-changed",
            &output_config_changed);
        output->connect_signal("workspace-changed", &viewport_changed);
        method.set_callback(method_changed);
        output->add_button(draw_binding, &draw_begin);
        output->add_activator(clear_binding, &clear_workspace);
        method_changed();
    }

    wf::config::option_base_t::updated_callback_t method_changed = [=] ()
    {
        if (std::string(method) == "draw")
        {
            draw_method = ANNOTATE_METHOD_DRAW;
        } else if (std::string(method) == "line")
        {
            draw_method = ANNOTATE_METHOD_LINE;
        } else if (std::string(method) == "rectangle")
        {
            draw_method = ANNOTATE_METHOD_RECTANGLE;
        } else if (std::string(method) == "circle")
        {
            draw_method = ANNOTATE_METHOD_CIRCLE;
        } else
        {
            draw_method = ANNOTATE_METHOD_DRAW;
        }
    };

    anno_ws_overlay& get_current_overlay()
    {
        auto ws = output->workspace->get_current_workspace();

        return overlays[ws.x][ws.y];
    }

    wf::signal_connection_t viewport_changed{[this] (wf::signal_data_t *data)
        {
            output->render->damage_whole();
        }
    };

    wf::button_callback draw_begin = [=] (wf::buttonbinding_t btn)
    {
        output->render->add_effect(&frame_pre_paint, wf::OUTPUT_EFFECT_DAMAGE);
        grab_point = last_cursor = wf::get_core().get_cursor_position();
        button     = btn.get_button();
        connect_ws_stream_post();

        grab();

        return true;
    };

    void draw_end()
    {
        auto& ol = get_current_overlay();

        output->render->rem_effect(&frame_pre_paint);
        overlay_destroy(shape_overlay);
        ungrab();

        switch (draw_method)
        {
          case ANNOTATE_METHOD_LINE:
            cairo_draw_line(ol, wf::get_core().get_cursor_position());
            break;

          case ANNOTATE_METHOD_RECTANGLE:
            cairo_draw_rectangle(ol, last_cursor);
            break;

          case ANNOTATE_METHOD_CIRCLE:
            cairo_draw_circle(ol, last_cursor);
            break;

          default:
            break;
        }
    }

    void deactivate_check()
    {
        bool all_workspaces_clear = true;
        auto wsize = output->workspace->get_workspace_grid_size();

        for (int x = 0; x < wsize.width; x++)
        {
            for (int y = 0; y < wsize.height; y++)
            {
                auto& ol = overlays[x][y];
                if (ol.cr)
                {
                    all_workspaces_clear = false;
                    x = wsize.width;
                    break;
                }
            }
        }

        if (all_workspaces_clear)
        {
            disconnect_ws_stream_post();
        }
    }

    void overlay_clear(anno_ws_overlay& ol)
    {
        if (!ol.cr)
        {
            return;
        }

        cairo_clear(ol.cr);
    }

    void overlay_destroy(anno_ws_overlay& ol)
    {
        if (!ol.cr)
        {
            return;
        }

        ol.texture.reset();
        cairo_surface_destroy(ol.cairo_surface);
        cairo_destroy(ol.cr);
        ol.cr = nullptr;
    }

    void clear()
    {
        auto& ol = get_current_overlay();

        overlay_destroy(ol);
        deactivate_check();

        output->render->damage_whole();
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

            clear();
        }
    };

    wf::activator_callback clear_workspace = [=] (auto)
    {
        clear();

        return true;
    };

    void cairo_init(anno_ws_overlay& ol)
    {
        auto og = output->get_relative_geometry();

        if (ol.cr)
        {
            return;
        }

        ol.cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, og.width,
            og.height);
        ol.cr = cairo_create(ol.cairo_surface);

        ol.texture = std::make_unique<wf::simple_texture_t>();
    }

    void cairo_clear(cairo_t *cr)
    {
        cairo_set_source_rgba(cr, 0, 0, 0, 0);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
    }

    void cairo_draw(anno_ws_overlay& ol, wf::pointf_t from, wf::pointf_t to)
    {
        auto og = output->get_layout_geometry();

        from.x -= og.x;
        from.y -= og.y;
        to.x   -= og.x;
        to.y   -= og.y;

        cairo_init(ol);
        cairo_t *cr = ol.cr;

        cairo_set_line_width(cr, line_width);
        cairo_set_source_rgba(cr,
            wf::color_t(stroke_color).r,
            wf::color_t(stroke_color).g,
            wf::color_t(stroke_color).b,
            wf::color_t(stroke_color).a);
        cairo_move_to(cr, from.x, from.y);
        cairo_line_to(cr, to.x, to.y);
        cairo_stroke(cr);

        OpenGL::render_begin();
        cairo_surface_upload_to_texture(ol.cairo_surface, *ol.texture);
        OpenGL::render_end();

        wlr_box bbox;
        int padding = line_width + 1;
        bbox.x     = std::min(from.x, to.x) - padding;
        bbox.y     = std::min(from.y, to.y) - padding;
        bbox.width = abs(from.x - to.x) + padding * 2;
        bbox.height = abs(from.y - to.y) + padding * 2;
        output->render->damage(bbox);
    }

    bool should_damage_last()
    {
        return shape_overlay.texture && shape_overlay.texture->tex != (uint32_t)-1;
    }

    void cairo_draw_line(anno_ws_overlay& ol, wf::pointf_t to)
    {
        auto og   = output->get_layout_geometry();
        auto from = grab_point;

        from.x -= og.x;
        from.y -= og.y;
        to.x   -= og.x;
        to.y   -= og.y;

        bool damage_last_bbox = should_damage_last();
        overlay_clear(shape_overlay);

        cairo_init(ol);
        cairo_t *cr = ol.cr;

        cairo_set_line_width(cr, line_width);
        cairo_set_source_rgba(cr,
            wf::color_t(stroke_color).r,
            wf::color_t(stroke_color).g,
            wf::color_t(stroke_color).b,
            wf::color_t(stroke_color).a);
        cairo_move_to(cr, from.x, from.y);
        cairo_line_to(cr, to.x, to.y);
        cairo_stroke(cr);

        OpenGL::render_begin();
        cairo_surface_upload_to_texture(ol.cairo_surface, *ol.texture);
        OpenGL::render_end();

        wlr_box bbox;
        int padding = line_width + 1;
        bbox.x     = std::min(from.x, to.x) - padding;
        bbox.y     = std::min(from.y, to.y) - padding;
        bbox.width = abs(from.x - to.x) + padding * 2;
        bbox.height = abs(from.y - to.y) + padding * 2;
        output->render->damage(bbox);
        if (damage_last_bbox)
        {
            output->render->damage(last_bbox);
        }

        last_bbox = bbox;
    }

    void cairo_draw_rectangle(anno_ws_overlay& ol, wf::pointf_t to)
    {
        auto og = output->get_layout_geometry();
        auto from = grab_point;
        double x, y, w, h;

        from.x -= og.x;
        from.y -= og.y;
        to.x   -= og.x;
        to.y   -= og.y;

        bool damage_last_bbox = should_damage_last();
        overlay_clear(shape_overlay);

        cairo_init(ol);
        cairo_t *cr = ol.cr;

        w = fabs(from.x - to.x);
        h = fabs(from.y - to.y);

        if (shapes_from_center)
        {
            x  = from.x - w;
            y  = from.y - h;
            w *= 2;
            h *= 2;
        } else
        {
            x = std::min(from.x, to.x);
            y = std::min(from.y, to.y);
        }

        cairo_set_line_width(cr, line_width);
        cairo_set_source_rgba(cr,
            wf::color_t(stroke_color).r,
            wf::color_t(stroke_color).g,
            wf::color_t(stroke_color).b,
            wf::color_t(stroke_color).a);
        cairo_rectangle(cr, x, y, w, h);
        cairo_stroke(cr);

        OpenGL::render_begin();
        cairo_surface_upload_to_texture(ol.cairo_surface, *ol.texture);
        OpenGL::render_end();

        wlr_box bbox;
        int padding = line_width + 1;
        bbox.x     = x - padding;
        bbox.y     = y - padding;
        bbox.width = w + padding * 2;
        bbox.height = h + padding * 2;
        output->render->damage(bbox);
        if (damage_last_bbox)
        {
            output->render->damage(last_bbox);
        }

        last_bbox = bbox;
    }

    void cairo_draw_circle(anno_ws_overlay& ol, wf::pointf_t to)
    {
        auto og   = output->get_layout_geometry();
        auto from = grab_point;

        from.x -= og.x;
        from.y -= og.y;
        to.x   -= og.x;
        to.y   -= og.y;

        bool damage_last_bbox = should_damage_last();
        overlay_clear(shape_overlay);

        cairo_init(ol);
        cairo_t *cr = ol.cr;

        auto radius =
            glm::distance(glm::vec2(from.x, from.y), glm::vec2(to.x, to.y));

        if (!shapes_from_center)
        {
            radius /= 2;
            from.x += (to.x - from.x) / 2;
            from.y += (to.y - from.y) / 2;
        }

        cairo_set_line_width(cr, line_width);
        cairo_set_source_rgba(cr,
            wf::color_t(stroke_color).r,
            wf::color_t(stroke_color).g,
            wf::color_t(stroke_color).b,
            wf::color_t(stroke_color).a);
        cairo_arc(cr, from.x, from.y, radius, 0, 2 * M_PI);
        cairo_stroke(cr);

        OpenGL::render_begin();
        cairo_surface_upload_to_texture(ol.cairo_surface, *ol.texture);
        OpenGL::render_end();

        wlr_box bbox;
        int padding = line_width + 1;
        bbox.x     = (from.x - radius) - padding;
        bbox.y     = (from.y - radius) - padding;
        bbox.width = (radius * 2) + padding * 2;
        bbox.height = (radius * 2) + padding * 2;
        output->render->damage(bbox);
        if (damage_last_bbox)
        {
            output->render->damage(last_bbox);
        }

        last_bbox = bbox;
    }

    wf::effect_hook_t frame_pre_paint = [=] ()
    {
        auto current_cursor = wf::get_core().get_cursor_position();
        auto& ol = get_current_overlay();

        switch (draw_method)
        {
          case ANNOTATE_METHOD_DRAW:
            cairo_draw(ol, last_cursor, current_cursor);
            break;

          case ANNOTATE_METHOD_LINE:
            cairo_draw_line(shape_overlay, current_cursor);
            break;

          case ANNOTATE_METHOD_RECTANGLE:
            cairo_draw_rectangle(shape_overlay, current_cursor);
            break;

          case ANNOTATE_METHOD_CIRCLE:
            cairo_draw_circle(shape_overlay, current_cursor);
            break;

          default:
            return;
        }

        last_cursor = current_cursor;
    };

    wf::signal_connection_t workspace_stream_post{[this] (wf::signal_data_t *data)
        {
            const auto& workspace = static_cast<wf::stream_signal_t*>(data);
            auto& ol    = overlays[workspace->ws.x][workspace->ws.y];
            auto og     = workspace->fb.geometry;
            auto damage = output->render->get_scheduled_damage() &
                output->render->get_ws_box(workspace->ws);

            OpenGL::render_begin(workspace->fb);
            for (auto& box : damage)
            {
                workspace->fb.logic_scissor(wlr_box_from_pixman_box(box));
                if (ol.cr)
                {
                    OpenGL::render_texture(wf::texture_t{ol.texture->tex},
                        workspace->fb, og, glm::vec4(1.0),
                        OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
                }

                if (shape_overlay.cr)
                {
                    OpenGL::render_texture(wf::texture_t{shape_overlay.texture->tex},
                        workspace->fb, og, glm::vec4(1.0),
                        OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
                }
            }

            OpenGL::render_end();
        }
    };

    void grab()
    {
        if (!output->activate_plugin(grab_interface))
        {
            return;
        }

        grab_interface->grab();
    }

    void ungrab()
    {
        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);
    }

    void connect_ws_stream_post()
    {
        if (hook_set)
        {
            return;
        }

        output->render->connect_signal("workspace-stream-post",
            &workspace_stream_post);
        hook_set = true;
    }

    void disconnect_ws_stream_post()
    {
        if (!hook_set)
        {
            return;
        }

        workspace_stream_post.disconnect();
        hook_set = false;
    }

    void fini() override
    {
        ungrab();
        disconnect_ws_stream_post();
        output->rem_binding(&draw_begin);
        output->rem_binding(&clear_workspace);
        auto wsize = output->workspace->get_workspace_grid_size();
        for (int x = 0; x < wsize.width; x++)
        {
            for (int y = 0; y < wsize.height; y++)
            {
                auto& ol = overlays[x][y];
                overlay_destroy(ol);
            }
        }

        output->render->damage_whole();
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_annotate_screen);
