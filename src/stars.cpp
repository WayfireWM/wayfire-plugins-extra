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
 * stars: a star field over the whole screen until toggled off -- either a
 * flight through space (stars stream outwards from the centre and grow as
 * they come closer) or a calm, twinkling night sky. Optional darkening of
 * the screen behind the stars.
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

/* Procedural star field: 8 depth layers, each a grid of cells with at most
 * one star. In "fly" mode every layer moves from far (small cells = many
 * tiny stars) to near (large cells, scaled around the screen centre), so
 * the stars stream outwards; a layer fades in when far and out when it
 * passes the viewer, then starts again far away. In "twinkle" mode the
 * layers stay at fixed depths. No state between frames. */
static const char *fragment_shader =
    R"(
#version 100
precision highp float;

uniform sampler2D u_texture;
uniform float time;
uniform float fly;        /* 1.0 = flight, 0.0 = twinkling sky */
uniform float density;    /* 0..1, chance that a cell holds a star */
uniform float star_size;  /* radius of the nearest stars in pixels */
uniform float darken;     /* 0..1, darkening of the screen behind */
uniform float fade;       /* 0..1 */
uniform vec2 resolution;  /* framebuffer size in pixels */
varying highp vec2 uvpos;

vec2 hash2(vec2 p)
{
    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)),
        dot(p, vec2(269.5, 183.3)))) * 43758.5453);
}

void main()
{
    vec4 screen = texture2D(u_texture, uvpos);
    if (fade <= 0.0)
    {
        gl_FragColor = screen;
        return;
    }

    /* centred, aspect-correct coordinates: 1.0 = screen height */
    vec2 uv = (uvpos * resolution - 0.5 * resolution) / resolution.y;
    vec3 light = vec3(0.0);

    for (int i = 0; i < 8; i++)
    {
        float layer = float(i);
        /* depth 0 = far .. 1 = near */
        float z = mix(layer / 8.0, fract(layer / 8.0 + time * 0.05), fly);
        float scale = mix(30.0, 1.5, z);              /* cells per screen height */
        float vis = smoothstep(0.0, 0.25, z) * (1.0 - smoothstep(0.85, 1.0, z) * fly);

        vec2 p = uv * scale + layer * vec2(17.3, 31.7);
        vec2 c = floor(p);
        vec2 f = p - c;
        vec2 r = hash2(c + layer * 7.13);
        if (r.x < density)
        {
            vec2 pos = 0.2 + 0.6 * hash2(c + 11.1 + layer);
            float rpx = star_size * mix(0.35, 1.0, z) * (0.6 + 0.4 * r.y);
            float rad = rpx * scale / resolution.y;    /* radius in cell units */
            float d = length(f - pos);
            float twinkle = 0.65 + 0.35 * sin(time * (2.0 + 4.0 * r.y) + r.x * 60.0);
            float b = (1.0 - smoothstep(rad * 0.2, rad, d)) * vis * twinkle;
            /* slightly blue or warm white */
            vec3 tint = mix(vec3(0.8, 0.88, 1.0), vec3(1.0, 0.92, 0.8), hash2(c + 5.0).x);
            light += tint * b;
        }
    }

    vec3 base = screen.rgb * (1.0 - darken * fade);
    gl_FragColor = vec4(clamp(base + light * fade, 0.0, 1.0), 1.0);
}
)";

class wayfire_stars_screen : public wf::per_output_plugin_instance_t
{
    wf::option_wrapper_t<std::string> mode_opt{"stars/mode"};
    wf::option_wrapper_t<int> density_opt{"stars/density"};
    wf::option_wrapper_t<int> size_opt{"stars/star_size"};
    wf::option_wrapper_t<int> speed_opt{"stars/speed"};
    wf::option_wrapper_t<int> darken_opt{"stars/darken"};
    wf::animation::simple_animation_t animation =
        wf::animation::simple_animation_t(wf::create_option<int>(1000));
    OpenGL::program_t program;
    bool shining     = false;
    bool hook_set    = false;
    double star_time = 0.0;
    std::chrono::steady_clock::time_point last_frame;

  public:
    void init() override
    {
        if (!wf::get_core().is_gles2())
        {
            LOGE("stars plugin requires GLES2 renderer!");
            return;
        }

        wf::gles::run_in_context_if_gles([&]
        {
            program.set_simple(OpenGL::compile_program(vertex_shader, fragment_shader));
        });
        animation.set(0, 0);
    }

    bool toggle()
    {
        if (!wf::get_core().is_gles2())
        {
            return false;
        }

        shining = !shining;
        if (shining)
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
        star_time += std::clamp(dt, 0.0, 0.1) * std::clamp(int(speed_opt), 10, 300) / 100.0;

        auto og  = output->get_relative_geometry();
        auto fbg = output->render->get_target_framebuffer().framebuffer_box_from_geometry_box(og);
        wf::gles_texture_t source_tex = wf::gles_texture_t::from_aux(source);

        wf::gles::run_in_context([&]
        {
            wf::gles::bind_render_buffer(dest);
            program.use(wf::TEXTURE_TYPE_RGBA);
            program.attrib_pointer("position", 2, 0, vertexData);
            program.attrib_pointer("uvPosition", 2, 0, coordData);
            program.uniform1f("time", float(star_time));
            program.uniform1f("fly", std::string(mode_opt) == "twinkle" ? 0.f : 1.f);
            program.uniform1f("density", std::clamp(int(density_opt), 1, 100) / 100.f);
            program.uniform1f("star_size", std::clamp(int(size_opt), 1, 10));
            program.uniform1f("darken", std::clamp(int(darken_opt), 0, 90) / 100.f);
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
        if (!shining && !animation.running())
        {
            hook_set = false;
            output->render->rem_effect(&damage_hook);
            output->render->rem_post(&render);
        }

        output->render->schedule_redraw();
    };

    void fini() override
    {
        shining = false;
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

/* Global part: key binding + IPC method stars/toggle for the focused output */
class wayfire_stars_global : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<wayfire_stars_screen>
{
    wf::ipc_activator_t toggle{"stars/toggle"};

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

DECLARE_WAYFIRE_PLUGIN(wayfire_stars_global);
