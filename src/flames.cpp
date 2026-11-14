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
 * flames: animated flames rising from the bottom edge of the screen, drawn
 * over everything until toggled off. Built on the post-hook structure of
 * water.cpp.
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

/* Value-noise fbm flames: noise scrolls upwards, is cut off by the distance
 * from the bottom edge and coloured with a black-body like ramp. */
static const char *fragment_shader =
    R"(
#version 100
precision highp float;

uniform sampler2D u_texture;
uniform float time;
uniform float height;     /* flame zone, fraction of the screen height */
uniform float intensity;  /* 0..1 */
uniform float fade;       /* 0..1, fade in/out */
uniform float aspect;     /* width / height */
uniform float flip;       /* 1.0 = draw from the top edge instead */
varying highp vec2 uvpos;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
        mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}

float fbm(vec2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; i++)
    {
        v += a * noise(p);
        p *= 2.03;
        a *= 0.5;
    }

    return v;
}

void main()
{
    /* In the post hook uv y = 0 is the top of the screen */
    float y = mix(1.0 - uvpos.y, uvpos.y, flip); /* 0 = bottom edge */
    float h = y / max(height, 0.01);             /* 0 bottom .. 1 top of zone */

    if (h > 1.3 || fade <= 0.0)
    {
        gl_FragColor = texture2D(u_texture, uvpos);
        return;
    }

    vec2 p = vec2(uvpos.x * aspect * 7.0, h * 3.0);
    /* every column gets its own, slowly changing tongue height */
    float tongue = 0.35 + 0.65 * fbm(vec2(uvpos.x * aspect * 3.5, time * 0.7));
    float hh = h / tongue;
    /* turbulence rising upwards */
    float turb = fbm(p - vec2(0.0, time * 2.2));
    float n = fbm(vec2(p.x * 0.5, p.y * 0.8) + vec2(time * 0.2, -time * 1.2));

    /* the turbulence scales the whole column, so even the base flickers */
    float flame = (1.0 - hh) * (0.35 + 1.25 * turb) + (n - 0.5) * 0.5;
    flame = clamp(flame * (0.45 + 0.75 * intensity), 0.0, 1.0);

    /* heat shimmer: bend the screen image slightly inside the flame zone */
    vec2 shimmer = vec2(n - 0.5, turb - 0.5) * 0.012 * clamp(1.2 - h, 0.0, 1.0) * fade;
    vec4 screen = texture2D(u_texture, uvpos + shimmer);

    /* dark red tips -> orange -> yellow core */
    vec3 color = vec3(smoothstep(0.0, 0.3, flame),
        smoothstep(0.3, 1.0, flame) * 0.8,
        smoothstep(0.85, 1.0, flame) * 0.3);
    float alpha = smoothstep(0.05, 0.5, flame) * 0.8 * fade;

    /* a little additive glow on top of the blend */
    gl_FragColor = vec4(clamp(mix(screen.rgb, color, alpha) + color * 0.12 * alpha,
        0.0, 1.0), 1.0);
}
)";

class wayfire_flames_screen : public wf::per_output_plugin_instance_t
{
    wf::option_wrapper_t<int> height_opt{"flames/height"};
    wf::option_wrapper_t<int> intensity_opt{"flames/intensity"};
    wf::option_wrapper_t<int> speed_opt{"flames/speed"};
    wf::option_wrapper_t<bool> flip_opt{"flames/flip"};
    wf::animation::simple_animation_t animation =
        wf::animation::simple_animation_t(wf::create_option<int>(1000));
    OpenGL::program_t program;
    bool burning  = false;
    bool hook_set = false;
    double flame_time = 0.0;
    std::chrono::steady_clock::time_point last_frame;

  public:
    void init() override
    {
        if (!wf::get_core().is_gles2())
        {
            LOGE("flames plugin requires GLES2 renderer!");
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

        burning = !burning;
        if (burning)
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
        last_frame  = now;
        flame_time += std::clamp(dt, 0.0, 0.1) * std::clamp(int(speed_opt), 10, 300) / 100.0;

        auto og = output->get_relative_geometry();
        wf::gles_texture_t source_tex = wf::gles_texture_t::from_aux(source);

        wf::gles::run_in_context([&]
        {
            wf::gles::bind_render_buffer(dest);
            program.use(wf::TEXTURE_TYPE_RGBA);
            program.attrib_pointer("position", 2, 0, vertexData);
            program.attrib_pointer("uvPosition", 2, 0, coordData);
            program.uniform1f("time", float(flame_time));
            program.uniform1f("height", std::clamp(int(height_opt), 5, 100) / 100.f);
            program.uniform1f("intensity", std::clamp(int(intensity_opt), 10, 100) / 100.f);
            program.uniform1f("fade", float(double(animation)));
            program.uniform1f("aspect", og.height > 0 ? float(og.width) / og.height : 1.f);
            program.uniform1f("flip", flip_opt ? 1.f : 0.f);
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, source_tex.tex_id));
            GL_CALL(glDisable(GL_BLEND));

            GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

            GL_CALL(glEnable(GL_BLEND));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            program.deactivate();
        });

        /* Fully faded out: remove the hooks, nothing is drawn any more */
        if (!burning && !animation.running())
        {
            hook_set = false;
            output->render->rem_effect(&damage_hook);
            output->render->rem_post(&render);
        }

        output->render->schedule_redraw();
    };

    void fini() override
    {
        burning = false;
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

/* Global part: key binding + IPC method flames/toggle for the focused output */
class wayfire_flames_global : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<wayfire_flames_screen>
{
    wf::ipc_activator_t toggle{"flames/toggle"};

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

DECLARE_WAYFIRE_PLUGIN(wayfire_flames_global);
