/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 Scott Moreau
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
#include <wayfire/touch/touch.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/per-output-plugin.hpp>


namespace wf
{
namespace showtouch
{
static const char *vertex_shader =
    R"(
#version 300 es

in mediump vec2 position;
in mediump vec2 texcoord;

out mediump vec2 uvpos;

void main() {

   gl_Position = vec4(position.xy, 0.0, 1.0);
   uvpos = texcoord;
}
)";

static const char *fragment_shader =
    R"(
#version 300 es
@builtin_ext@
@builtin@

precision mediump float;

out vec4 out_color;
in mediump vec2 uvpos;
uniform vec2 resolution;
uniform vec2 finger0;
uniform vec2 finger1;
uniform vec2 finger2;
uniform vec2 finger3;
uniform vec2 finger4;
uniform vec2 center;
float radius = 25.0;

void main()
{
    vec4 c = get_pixel(uvpos);
    float m = distance(uvpos * resolution, finger0);
    if (m < radius)
        c = mix(vec4(1.0, 0.0, 0.2, 1.0), c, m / (radius * 2.0));
    m = distance(uvpos * resolution, finger1);
    if (m < radius)
        c = mix(vec4(1.0, 0.0, 0.4, 1.0), c, m / (radius * 2.0));
    m = distance(uvpos * resolution, finger2);
    if (m < radius)
        c = mix(vec4(1.0, 0.0, 0.6, 1.0), c, m / (radius * 2.0));
    m = distance(uvpos * resolution, finger3);
    if (m < radius)
        c = mix(vec4(1.0, 0.0, 0.8, 1.0), c, m / (radius * 2.0));
    m = distance(uvpos * resolution, finger4);
    if (m < radius)
        c = mix(vec4(1.0, 0.0, 1.0, 1.0), c, m / (radius * 2.0));
    m = distance(uvpos * resolution, center);
    if (m < radius)
        c = mix(vec4(1.0, 1.0, 0.0, 1.0), c, m / (radius * 2.0));
    out_color = c;
}
)";

class wayfire_showtouch : public wf::per_output_plugin_instance_t
{
    wf::option_wrapper_t<wf::color_t> finger_color{"showtouch/finger_color"};
    wf::option_wrapper_t<wf::color_t> center_color{"showtouch/center_color"};

    OpenGL::program_t program;

  public:
    void init() override
    {
        OpenGL::render_begin();
        program.compile(vertex_shader, fragment_shader);
        OpenGL::render_end();
        output->render->add_post(&post_hook);
        output->render->add_effect(&frame_pre_paint, wf::OUTPUT_EFFECT_DAMAGE);
    }

    wf::effect_hook_t frame_pre_paint = [=] ()
    {
        output->render->damage_whole();
    };

    wf::post_hook_t post_hook = [=] (const wf::framebuffer_t& source,
                                     const wf::framebuffer_t& dest)
    {
        static const float vertexData[] = {
            -1.0f, -1.0f,
            1.0f, -1.0f,
            1.0f, 1.0f,
            -1.0f, 1.0f
        };
        static const float texCoords[] = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 1.0f
        };

        auto og = output->get_relative_geometry();

        OpenGL::render_begin(dest);
        program.use(wf::TEXTURE_TYPE_RGBA);
        program.set_active_texture(wf::texture_t{source.tex});
        for (int i = 0; i < 5; i++)
        {
            program.uniform2f("finger" + std::to_string(i), -100, -100);
        }

        const auto& touch_state = wf::get_core().get_touch_state();
        for (auto& finger : touch_state.fingers)
        {
            auto n = finger.first;
            auto f = finger.second.current;
            program.uniform2f("finger" + std::to_string(n), f.x, f.y);
        }

        const auto c = touch_state.get_center().current;
        program.uniform2f("center", c.x, c.y);
        program.attrib_pointer("position", 2, 0, vertexData);
        program.attrib_pointer("texcoord", 2, 0, texCoords);
        program.uniform2f("resolution", og.width, og.height);

        GL_CALL(glEnable(GL_BLEND));
        GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

        GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
        GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));

        program.deactivate();
        OpenGL::render_end();
    };

    void fini() override
    {
        output->render->rem_post(&post_hook);
        output->render->rem_effect(&frame_pre_paint);
        output->render->damage_whole();
        program.free_resources();
    }
};
}
}

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wf::showtouch::wayfire_showtouch>);
