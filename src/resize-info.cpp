/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Ahmed Abdelhaleem Ahmed
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
 *
 * resize-info: shows the size of a window ("800 × 600") in its centre
 * while it is being resized, like Compiz's Resize Info. On every frame
 * (pre-render effect hook, almost free) it checks whether the upstream
 * "resize" plugin is active on the output; while it is, the size of the
 * active (= resized) window is rendered with cairo/pango (re-rendered only
 * when it changes) and drawn over the frame in a post hook.
 */

#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>
#include <algorithm>
#include <cmath>
#include <string>

static const char *vertex_shader =
    R"(
#version 100
attribute highp vec2 position;
attribute highp vec2 uvPosition;
varying highp vec2 uvpos;
void main()
{
    gl_Position = vec4(position.xy, 0.0, 1.0);
    uvpos = uvPosition;
}
)";

/* plain copy (frame) and premultiplied texture (label) */
static const char *fragment_shader =
    R"(
#version 100
precision highp float;
uniform sampler2D u_texture;
uniform float opaque;
varying highp vec2 uvpos;
void main()
{
    vec4 c = texture2D(u_texture, uvpos);
    gl_FragColor = mix(c, vec4(c.rgb, 1.0), opaque);
}
)";

class wayfire_resize_info_screen : public wf::per_output_plugin_instance_t
{
    wf::option_wrapper_t<int> font_size{"resize-info/font_size"};
    wf::option_wrapper_t<wf::color_t> text_color{"resize-info/text_color"};
    wf::option_wrapper_t<wf::color_t> bg_color{"resize-info/bg_color"};

    OpenGL::program_t program;
    wf::cairo_text_t label;
    std::string label_text;
    wf::geometry_t view_box; /* output-local, logical */
    bool showing  = false;
    bool post_set = false;

    void show(const std::string& text, wf::geometry_t box)
    {
        if (text != label_text)
        {
            wf::cairo_text_t::params par(std::clamp(int(font_size), 8, 72), bg_color, text_color,
                output->handle->scale, {0, 0}, true, true);
            label.render_text(text, par);
            label_text = text;
        }

        view_box = box;
        showing  = true;
        if (!post_set)
        {
            output->render->add_post(&render);
            post_set = true;
        }

        output->render->damage_whole();
    }

    void hide()
    {
        showing = false;
        label_text.clear();
        if (post_set)
        {
            output->render->rem_post(&render);
            post_set = false;
            output->render->damage_whole();
        }
    }

    /* runs before every frame of this output */
    wf::effect_hook_t check = [=] ()
    {
        auto view = wf::toplevel_cast(wf::get_core().seat->get_active_view());
        if (!output->is_plugin_active("resize") || !view || (view->get_output() != output))
        {
            if (showing)
            {
                hide();
            }

            return;
        }

        auto g = view->get_geometry();
        /* geometry values may be floating point: show whole pixels */
        show(std::to_string(std::lround(g.width)) + " \u00d7 " +
            std::to_string(std::lround(g.height)), g);
    };

    wf::post_hook_t render = [=] (wf::auxilliary_buffer_t& source, const wf::render_buffer_t& dest)
    {
        static const float vertexData[] = {-1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f};
        static const float coordData[]  = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};

        auto og = output->get_relative_geometry();
        auto fbg = output->render->get_target_framebuffer().framebuffer_box_from_geometry_box(og);
        float W = std::max(1.0f, float(fbg.width)), H = std::max(1.0f, float(fbg.height));
        float sx = W / std::max(1.0f, float(og.width));
        float sy = H / std::max(1.0f, float(og.height));
        wf::gles_texture_t source_tex = wf::gles_texture_t::from_aux(source);

        /* label rectangle in framebuffer pixels, centred on the window; in
         * the post hook's target NDC y = -1 is the top (see firepaint) */
        float lw = 0, lh = 0, cx = 0, cy = 0;
        GLuint label_tex = 0;
        GLenum label_target = GL_TEXTURE_2D;
        if (showing && label.get_texture())
        {
            auto size = label.get_size();
            lw = size.width;
            lh = size.height;
            cx = (view_box.x + view_box.width / 2.0f) * sx;
            cy = (view_box.y + view_box.height / 2.0f) * sy;
            wf::gles_texture_t gt(label.get_texture());
            label_tex    = gt.tex_id;
            label_target = gt.target;
        }

        float x0 = (cx - lw / 2) / W * 2 - 1, x1 = (cx + lw / 2) / W * 2 - 1;
        float y0 = (cy - lh / 2) / H * 2 - 1, y1 = (cy + lh / 2) / H * 2 - 1;
        const float labelVerts[] = {x0, y0, x1, y0, x1, y1, x0, y1};

        wf::gles::run_in_context([&]
        {
            wf::gles::bind_render_buffer(dest);
            program.use(wf::TEXTURE_TYPE_RGBA);
            GL_CALL(glActiveTexture(GL_TEXTURE0));

            /* the frame */
            program.attrib_pointer("position", 2, 0, vertexData);
            program.attrib_pointer("uvPosition", 2, 0, coordData);
            program.uniform1f("opaque", 1.0f);
            GL_CALL(glBindTexture(GL_TEXTURE_2D, source_tex.tex_id));
            GL_CALL(glDisable(GL_BLEND));
            GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

            /* the label (premultiplied alpha from cairo) */
            if (label_tex)
            {
                program.attrib_pointer("position", 2, 0, labelVerts);
                program.attrib_pointer("uvPosition", 2, 0, coordData);
                program.uniform1f("opaque", 0.0f);
                GL_CALL(glBindTexture(label_target, label_tex));
                /* a fresh wlr texture has no filter set; the default
                 * minification filter needs mipmaps, which it does not have,
                 * so sampling would return black */
                GL_CALL(glTexParameteri(label_target, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
                GL_CALL(glTexParameteri(label_target, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
                GL_CALL(glEnable(GL_BLEND));
                GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
                GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
            }

            GL_CALL(glEnable(GL_BLEND));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            program.deactivate();
        });
    };

  public:
    void init() override
    {
        if (!wf::get_core().is_gles2())
        {
            LOGE("resize-info plugin requires GLES2 renderer!");
            return;
        }

        wf::gles::run_in_context_if_gles([&]
        {
            program.set_simple(OpenGL::compile_program(vertex_shader, fragment_shader));
        });
        output->render->add_effect(&check, wf::OUTPUT_EFFECT_PRE);
    }

    void fini() override
    {
        output->render->rem_effect(&check);
        hide();
        wf::gles::run_in_context_if_gles([&]
        {
            program.free_resources();
        });
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_resize_info_screen>);
