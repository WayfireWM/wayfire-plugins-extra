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
 * fireflies: glowing dots wandering and blinking over the whole screen until
 * toggled off. Paths are computed on the CPU (at most 64 fireflies), the
 * shader only draws the glows.
 */

#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugins/ipc/ipc-activator.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

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

#define MAX_FIREFLIES 64

/* Additive glow: a wide soft halo and a small bright core per firefly */
static const char *fragment_shader =
    R"(
#version 100
precision highp float;

uniform sampler2D u_texture;
uniform vec3 flies[64];   /* x, y in pixels (y = 0 at the top), brightness */
uniform int count;
uniform float radius;     /* halo radius in pixels */
uniform vec3 color;
uniform float fade;       /* 0..1 */
uniform vec2 resolution;
varying highp vec2 uvpos;

void main()
{
    vec4 screen = texture2D(u_texture, uvpos);
    if (fade <= 0.0)
    {
        gl_FragColor = screen;
        return;
    }

    vec2 px = uvpos * resolution;
    float r2 = radius * radius;
    float glow = 0.0;
    float core = 0.0;
    for (int i = 0; i < 64; i++)
    {
        if (i >= count)
        {
            break;
        }

        vec2 d = px - flies[i].xy;
        float dd = dot(d, d);
        glow += flies[i].z * exp(-dd / r2) * 0.7;
        core += flies[i].z * exp(-dd / (r2 * 0.04));
    }

    vec3 c = screen.rgb + (color * glow + mix(color, vec3(1.0), 0.6) * core) * fade;
    gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

class wayfire_fireflies_screen : public wf::per_output_plugin_instance_t
{
    wf::option_wrapper_t<int> count_opt{"fireflies/count"};
    wf::option_wrapper_t<int> size_opt{"fireflies/size"};
    wf::option_wrapper_t<int> speed_opt{"fireflies/speed"};
    wf::option_wrapper_t<wf::color_t> color_opt{"fireflies/color"};
    wf::animation::simple_animation_t animation =
        wf::animation::simple_animation_t(wf::create_option<int>(1000));
    OpenGL::program_t program;
    bool glowing    = false;
    bool hook_set   = false;
    double fly_time = 0.0;
    int flies_loc   = -1;
    std::chrono::steady_clock::time_point last_frame;

  public:
    void init() override
    {
        if (!wf::get_core().is_gles2())
        {
            LOGE("fireflies plugin requires GLES2 renderer!");
            return;
        }

        wf::gles::run_in_context_if_gles([&]
        {
            program.set_simple(OpenGL::compile_program(vertex_shader, fragment_shader));
            flies_loc = GL_CALL(glGetUniformLocation(
                program.get_program_id(wf::TEXTURE_TYPE_RGBA), "flies"));
        });
        animation.set(0, 0);
    }

    bool toggle()
    {
        if (!wf::get_core().is_gles2())
        {
            return false;
        }

        glowing = !glowing;
        if (glowing)
        {
            if (!hook_set)
            {
                output->render->add_effect(&damage_hook, wf::OUTPUT_EFFECT_DAMAGE);
                output->render->add_post(&render);
                hook_set   = true;
                last_frame = std::chrono::steady_clock::now();
            }

            animation.animate(animation, 1);
        } else
        {
            animation.animate(animation, 0);
        }

        output->render->schedule_redraw();
        return true;
    }

    wf::effect_hook_t damage_hook = [=] ()
    {
        output->render->damage_whole();
    };

    wf::post_hook_t render = [=] (wf::auxilliary_buffer_t& source, const wf::render_buffer_t& dest)
    {
        static const float vertexData[] = {
            -1.0f, -1.0f,
            1.0f, -1.0f,
            1.0f, 1.0f,
            -1.0f, 1.0f
        };

        static const float coordData[] = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,
            0.0f, 1.0f
        };

        auto now  = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last_frame).count();
        last_frame = now;
        fly_time  += std::clamp(dt, 0.0, 0.1) * std::clamp(int(speed_opt), 10, 300) / 100.0;

        auto og  = output->get_relative_geometry();
        auto fbg = output->render->get_target_framebuffer().framebuffer_box_from_geometry_box(og);
        wf::gles_texture_t source_tex = wf::gles_texture_t::from_aux(source);

        /* Each firefly wanders on two overlaid slow sine paths around its own
        * home position (wrapping at the screen edges) and blinks. All
        * parameters come from a fixed per-index hash, so paths are stable. */
        int count = std::clamp(int(count_opt), 1, MAX_FIREFLIES);
        std::vector<float> flies;
        flies.reserve(count * 3);
        /* integer hash (lowbias32) of (index, parameter) -> [0, 1) */
        auto h = [] (int i, int k)
        {
            uint32_t x = uint32_t(i) * 747796405u + uint32_t(k) * 2891336453u + 12345u;
            x ^= x >> 16;
            x *= 0x7feb352du;
            x ^= x >> 15;
            x *= 0x846ca68bu;
            x ^= x >> 16;
            return float(x) / 4294967296.0f;
        };
        float t = float(fly_time);
        for (int i = 0; i < count; i++)
        {
            float x = h(i, 1) + 0.12f * std::sin(t * (0.15f + 0.25f * h(i, 2)) + 6.283f * h(i, 3)) +
                0.04f * std::sin(t * (0.6f + 0.6f * h(i, 4)) + 6.283f * h(i, 5));
            float y = h(i, 6) + 0.10f * std::sin(t * (0.12f + 0.22f * h(i, 7)) + 6.283f * h(i, 8)) +
                0.04f * std::cos(t * (0.5f + 0.7f * h(i, 9)) + 6.283f * h(i, 10));
            x -= std::floor(x);
            y -= std::floor(y);
            float blink = 0.5f + 0.5f * std::sin(t * (0.8f + 1.6f * h(i, 11)) + 6.283f * h(i, 12));
            blink = blink * blink * blink;
            flies.push_back(x * fbg.width);
            flies.push_back(y * fbg.height);
            flies.push_back(0.15f + 0.85f * blink);
        }

        wf::color_t col = color_opt;
        wf::gles::run_in_context([&]
        {
            wf::gles::bind_render_buffer(dest);
            program.use(wf::TEXTURE_TYPE_RGBA);
            program.attrib_pointer("position", 2, 0, vertexData);
            program.attrib_pointer("uvPosition", 2, 0, coordData);
            GL_CALL(glUniform3fv(flies_loc, count, flies.data()));
            program.uniform1i("count", count);
            program.uniform1f("radius", std::clamp(int(size_opt), 2, 40));
            program.uniform3f("color", col.r, col.g, col.b);
            program.uniform1f("fade", float(double(animation)));
            program.uniform2f("resolution", fbg.width, fbg.height);
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, source_tex.tex_id));
            GL_CALL(glDisable(GL_BLEND));

            GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

            GL_CALL(glEnable(GL_BLEND));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            program.deactivate();
        });

        /* Fully faded out: remove the hooks, nothing is drawn any more */
        if (!glowing && !animation.running())
        {
            hook_set = false;
            output->render->rem_effect(&damage_hook);
            output->render->rem_post(&render);
        }

        output->render->schedule_redraw();
    };

    void fini() override
    {
        glowing = false;
        if (hook_set)
        {
            output->render->rem_effect(&damage_hook);
            output->render->rem_post(&render);
            hook_set = false;
        }

        wf::gles::run_in_context_if_gles([&]
        {
            program.free_resources();
        });
    }
};

/* Global part: key binding + IPC method fireflies/toggle for the focused output */
class wayfire_fireflies_global : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<wayfire_fireflies_screen>
{
    wf::ipc_activator_t toggle{"fireflies/toggle"};

  public:
    void init() override
    {
        this->init_output_tracking();
        toggle.set_handler(toggle_cb);
    }

    void fini() override
    {
        this->fini_output_tracking();
    }

    wf::ipc_activator_t::handler_t toggle_cb = [=] (wf::output_t *output, wayfire_view)
    {
        return this->output_instance[output]->toggle();
    };
};

DECLARE_WAYFIRE_PLUGIN(wayfire_fireflies_global);
