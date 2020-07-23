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

#include <math.h>
#include <deque>
#include <numeric>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>

extern "C"
{
#include <wlr/types/wlr_output.h>
}

#define WIDGET_PADDING 10

class wayfire_bench_screen : public wf::plugin_interface_t
{
    cairo_t *cr = nullptr;
    double text_y;
    double max_fps = 0;
    double widget_xc;
    uint32_t last_time = wf::get_current_time();
    double current_fps;
    double widget_radius;
    wf::simple_texture_t bench_tex;
    wf::geometry_t cairo_geometry;
    cairo_surface_t *cairo_surface;
    cairo_text_extents_t text_extents;
    std::deque<int> last_frame_times;
    int frames_since_last_update = 0;
    wf::option_wrapper_t<std::string> position{"bench/position"};
    wf::option_wrapper_t<int> average_frames{"bench/average_frames"};
    wf::option_wrapper_t<int> frames_per_update{"bench/frames_per_update"};

  public:
    void init() override
    {
        grab_interface->name = "bench";
        grab_interface->capabilities = 0;

        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        output->render->add_effect(&overlay_hook, wf::OUTPUT_EFFECT_OVERLAY);
        output->render->set_redraw_always();

        output->connect_signal("reserved-workarea", &workarea_changed);
        position.set_callback(position_changed);
        update_texture_position();
    }

    wf::config::option_base_t::updated_callback_t position_changed = [=] ()
    {
        update_texture_position();
    };

    void cairo_recreate()
    {
        auto og = output->get_relative_geometry();
        auto font_size = og.height * 0.05;

        if (!cr)
        {
            /* Setup dummy context to get initial font size */
            cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
            cr = cairo_create(cairo_surface);
        }

        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
            CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, font_size);

        cairo_text_extents(cr, "234.5", &text_extents);

        widget_xc = text_extents.width / 2 + text_extents.x_bearing + WIDGET_PADDING;
        text_y    = text_extents.height + WIDGET_PADDING;
        widget_radius = og.height * 0.04;

        cairo_geometry.width  = text_extents.width + WIDGET_PADDING * 2;
        cairo_geometry.height = text_extents.height + widget_radius +
            (widget_radius * sin(M_PI / 8)) + WIDGET_PADDING * 2;

        /* Recreate surface based on font size */
        cairo_destroy(cr);
        cairo_surface_destroy(cairo_surface);

        cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
            cairo_geometry.width, cairo_geometry.height);
        cr = cairo_create(cairo_surface);

        cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL,
            CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, font_size);
    }

    void update_texture_position()
    {
        auto workarea = output->workspace->get_workarea();

        cairo_recreate();

        if ((std::string)position == "top_left")
        {
            cairo_geometry.x = workarea.x;
            cairo_geometry.y = workarea.y;
        } else if ((std::string)position == "top_center")
        {
            cairo_geometry.x = workarea.x +
                (workarea.width / 2 - cairo_geometry.width / 2);
            cairo_geometry.y = workarea.y;
        } else if ((std::string)position == "top_right")
        {
            cairo_geometry.x = workarea.x + (workarea.width - cairo_geometry.width);
            cairo_geometry.y = workarea.y;
        } else if ((std::string)position == "center_left")
        {
            cairo_geometry.x = workarea.x;
            cairo_geometry.y = workarea.y +
                (workarea.height / 2 - cairo_geometry.height / 2);
        } else if ((std::string)position == "center")
        {
            cairo_geometry.x = workarea.x +
                (workarea.width / 2 - cairo_geometry.width / 2);
            cairo_geometry.y = workarea.y +
                (workarea.height / 2 - cairo_geometry.height / 2);
        } else if ((std::string)position == "center_right")
        {
            cairo_geometry.x = workarea.x + (workarea.width - cairo_geometry.width);
            cairo_geometry.y = workarea.y +
                (workarea.height / 2 - cairo_geometry.height / 2);
        } else if ((std::string)position == "bottom_left")
        {
            cairo_geometry.x = workarea.x;
            cairo_geometry.y = workarea.y +
                (workarea.height - cairo_geometry.height);
        } else if ((std::string)position == "bottom_center")
        {
            cairo_geometry.x = workarea.x +
                (workarea.width / 2 - cairo_geometry.width / 2);
            cairo_geometry.y = workarea.y +
                (workarea.height - cairo_geometry.height);
        } else if ((std::string)position == "bottom_right")
        {
            cairo_geometry.x = workarea.x + (workarea.width - cairo_geometry.width);
            cairo_geometry.y = workarea.y +
                (workarea.height - cairo_geometry.height);
        } else
        {
            cairo_geometry.x = workarea.x;
            cairo_geometry.y = workarea.y;
        }

        output->render->damage_whole();
    }

    wf::signal_connection_t workarea_changed{[this] (wf::signal_data_t *data)
        {
            update_texture_position();
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

    void render_bench()
    {
        double xc     = widget_xc;
        double yc     = widget_radius + WIDGET_PADDING;
        double radius = widget_radius;
        double min_angle    = M_PI / 8;
        double max_angle    = M_PI - M_PI / 8;
        double target_angle = 2 * M_PI - M_PI / 8;
        double fps_angle;
        char fps_buf[128];

        double average = std::accumulate(
            last_frame_times.begin(), last_frame_times.end(), 0.0);
        average /= last_frame_times.size();

        current_fps = (double)1000 / average;

        if (current_fps > max_fps)
        {
            max_fps = current_fps;
        } else
        {
            max_fps -= 1;
        }

        sprintf(fps_buf, "%.1f", current_fps);

        if (output->handle->current_mode)
        {
            fps_angle = max_angle + (current_fps /
                ((double)output->handle->current_mode->refresh / 1000)) *
                (target_angle - max_angle);
        } else
        {
            fps_angle = max_angle + (current_fps / max_fps) *
                (target_angle - max_angle);
        }

        cairo_clear(cr);

        cairo_set_line_width(cr, 5.0);

        cairo_set_source_rgba_swizzle(cr, 0, 0, 0, 1);
        cairo_arc_negative(cr, xc, yc, radius, min_angle, max_angle);
        cairo_stroke(cr);

        cairo_set_source_rgba_swizzle(cr, 0.7, 0.7, 0.7, 0.7);
        cairo_move_to(cr, xc, yc);
        cairo_arc_negative(cr, xc, yc, radius, min_angle, max_angle);
        cairo_fill(cr);

        cairo_set_source_rgba_swizzle(cr, 1.0, 0.2, 0.2, 0.7);
        cairo_move_to(cr, xc, yc);
        cairo_arc_negative(cr, xc, yc, radius, fps_angle, max_angle);
        cairo_fill(cr);

        if (output->handle->current_mode)
        {
            cairo_set_source_rgba_swizzle(cr, 0, 0, 1, 1);
        } else
        {
            cairo_set_source_rgba_swizzle(cr, 1, 1, 0, 1);
        }

        cairo_text_extents(cr, fps_buf, &text_extents);
        cairo_move_to(cr,
            xc - (text_extents.width / 2 + text_extents.x_bearing),
            text_y + yc);
        cairo_show_text(cr, fps_buf);
        cairo_stroke(cr);

        OpenGL::render_begin();
        cairo_surface_upload_to_texture(cairo_surface, bench_tex);
        OpenGL::render_end();
    }

    wf::effect_hook_t pre_hook = [=] ()
    {
        uint32_t current_time = wf::get_current_time();
        uint32_t elapsed = current_time - last_time;

        while ((int)last_frame_times.size() >= average_frames)
        {
            last_frame_times.pop_front();
        }

        last_frame_times.push_back(elapsed);

        if (++frames_since_last_update >= frames_per_update)
        {
            render_bench();
            frames_since_last_update = 0;
        }

        last_time = current_time;
        output->render->damage(cairo_geometry);
    };

    wf::effect_hook_t overlay_hook = [=] ()
    {
        auto fb = output->render->get_target_framebuffer();
        OpenGL::render_begin(fb);
        OpenGL::render_transformed_texture(wf::texture_t{bench_tex.tex},
            cairo_geometry, fb.get_orthographic_projection(), glm::vec4(1.0),
            OpenGL::TEXTURE_TRANSFORM_INVERT_Y);
        OpenGL::render_end();
    };

    void fini() override
    {
        output->render->set_redraw_always(false);
        output->render->rem_effect(&pre_hook);
        output->render->rem_effect(&overlay_hook);
        cairo_surface_destroy(cairo_surface);
        cairo_destroy(cr);
        output->render->damage(cairo_geometry);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_bench_screen);
