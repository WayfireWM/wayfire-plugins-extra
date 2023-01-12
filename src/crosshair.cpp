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

#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/per-output-plugin.hpp>

class wayfire_crosshair : public wf::per_output_plugin_instance_t
{
    wf::option_wrapper_t<int> line_width{"crosshair/line_width"};
    wf::option_wrapper_t<wf::color_t> line_color{"crosshair/line_color"};
    wf::geometry_t geometry[2];

    OpenGL::program_t program;

  public:
    void init() override
    {
        output->render->add_effect(&post_hook, wf::OUTPUT_EFFECT_OVERLAY);
        output->render->add_effect(&frame_pre_paint, wf::OUTPUT_EFFECT_DAMAGE);
    }

    wf::effect_hook_t frame_pre_paint = [=] ()
    {
        auto oc = output->get_cursor_position();
        auto og = output->get_relative_geometry();

        float half_width = line_width * 0.5;

        /* Damage last frame geometry to clear it */
        output->render->damage(geometry[0]);
        output->render->damage(geometry[1]);

        geometry[0] =
            wf::geometry_t{int(oc.x - half_width), 0, line_width, og.height};
        geometry[1] =
            wf::geometry_t{0, int(oc.y - half_width), og.width, line_width};

        output->render->damage(geometry[0]);
        output->render->damage(geometry[1]);
    };

    wf::effect_hook_t post_hook = [=] ()
    {
        auto target_fb = output->render->get_target_framebuffer();
        auto gc = wf::get_core().get_cursor_position();
        wf::point_t coords{(int)gc.x, (int)gc.y};

        if (!(output->get_layout_geometry() & coords))
        {
            return;
        }

        wf::region_t region;
        region |= geometry[0];
        region |= geometry[1];
        region &= output->render->get_swap_damage();

        auto alpha = wf::color_t(line_color).a;
        wf::color_t color = wf::color_t{
            wf::color_t(line_color).r * alpha,
            wf::color_t(line_color).g * alpha,
            wf::color_t(line_color).b * alpha,
            alpha};

        OpenGL::render_begin(target_fb);
        for (auto& b : region)
        {
            OpenGL::render_rectangle(wlr_box_from_pixman_box(b), color,
                target_fb.get_orthographic_projection());
        }

        OpenGL::render_end();
    };

    void fini() override
    {
        output->render->rem_effect(&post_hook);
        output->render->rem_effect(&frame_pre_paint);
        output->render->damage_whole();
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_crosshair>);
