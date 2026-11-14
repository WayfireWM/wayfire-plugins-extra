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
 * colorfilter: a colour filter over the whole screen until toggled off --
 * greyscale, sepia, night (warm tint), simulation of protanopia /
 * deuteranopia / tritanopia, and correction (daltonize) for them.
 * Unlike the particle effects it does not animate: the screen is only
 * re-filtered when it changes anyway (no extra redraws while on).
 */

#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugins/ipc/ipc-activator.hpp>
#include <algorithm>
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

// out = m * in (rows m0..m2), optionally in linear light, optionally with
// the daltonize error shift; mixed with the original by strength * fade
static const char *fragment_shader =
    R"(
#version 100
precision highp float;

uniform sampler2D u_texture;
uniform vec3 m0;
uniform vec3 m1;
uniform vec3 m2;
uniform float linearize;  /* 1.0: apply the matrix in linear light */
uniform float daltonize;  /* 1.0: correct instead of simulate */
uniform float strength;   /* 0..1 */
uniform float fade;       /* 0..1 */
varying highp vec2 uvpos;

void main()
{
    vec4 screen = texture2D(u_texture, uvpos);
    vec3 c = screen.rgb;
    vec3 lin = mix(c, pow(c, vec3(2.2)), linearize);
    vec3 f = vec3(dot(m0, lin), dot(m1, lin), dot(m2, lin));
    if (daltonize > 0.5)
    {
        /* what the viewer cannot see, moved into channels they can see */
        vec3 err = lin - f;
        f = lin + vec3(0.0, 0.7 * err.r + err.g, 0.7 * err.r + err.b);
    }

    f = clamp(f, 0.0, 1.0);
    f = mix(f, pow(f, vec3(1.0 / 2.2)), linearize);
    gl_FragColor = vec4(mix(c, f, strength * fade), 1.0);
}
)";

struct filter_def
{
    const char *name;
    float m[9];
    bool linear;
    bool daltonize;
};

/* Colour-vision-deficiency matrices: Machado, Oliveira, Fernandes (2009),
 * severity 1.0, linear RGB. Every row sums to 1 (white stays white). */
static const filter_def FILTERS[] = {
    {"greyscale", {0.2126f, 0.7152f, 0.0722f, 0.2126f, 0.7152f, 0.0722f, 0.2126f, 0.7152f, 0.0722f},
        true, false},
    {"sepia", {0.393f, 0.769f, 0.189f, 0.349f, 0.686f, 0.168f, 0.272f, 0.534f, 0.131f}, false, false},
    {"night", {1.0f, 0.0f, 0.0f, 0.0f, 0.78f, 0.0f, 0.0f, 0.0f, 0.55f}, false, false},
    {"protanopia", {0.152286f, 1.052583f, -0.204868f, 0.114503f, 0.786281f, 0.099216f,
            -0.003882f, -0.048116f, 1.051998f}, true, false},
    {"deuteranopia", {0.367322f, 0.860646f, -0.227968f, 0.280085f, 0.672501f, 0.047413f,
            -0.011820f, 0.042940f, 0.968881f}, true, false},
    {"tritanopia", {1.255528f, -0.076749f, -0.178779f, -0.078411f, 0.930809f, 0.147602f,
            0.004733f, 0.691367f, 0.303900f}, true, false},
};

class wayfire_colorfilter_screen : public wf::per_output_plugin_instance_t
{
    wf::option_wrapper_t<std::string> filter_opt{"colorfilter/filter"};
    wf::option_wrapper_t<int> strength_opt{"colorfilter/strength"};
    wf::animation::simple_animation_t animation =
        wf::animation::simple_animation_t(wf::create_option<int>(400));
    OpenGL::program_t program;
    bool active   = false;
    bool hook_set = false;

    /* "protanopia-correct" etc. = the simulation matrix + daltonize */
    void select(const filter_def*& def, bool& correct)
    {
        std::string name = filter_opt;
        correct = false;
        const std::string suffix = "-correct";
        if ((name.size() > suffix.size()) &&
            (name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0))
        {
            correct = true;
            name    = name.substr(0, name.size() - suffix.size());
        }

        def = &FILTERS[0];
        for (auto& f : FILTERS)
        {
            if (name == f.name)
            {
                def = &f;
            }
        }

        if (!def->linear) /* only the colour-vision filters can correct */
        {
            correct = false;
        }
    }

  public:
    void init() override
    {
        if (!wf::get_core().is_gles2())
        {
            LOGE("colorfilter plugin requires GLES2 renderer!");
            return;
        }

        wf::gles::run_in_context_if_gles([&]
        {
            program.set_simple(OpenGL::compile_program(vertex_shader, fragment_shader));
        });
        animation.set(0, 0);
        /* re-filter immediately when the filter or strength is changed */
        filter_opt.set_callback([=]
        {
            if (hook_set)
            {
                output->render->damage_whole();
            }
        });
        strength_opt.set_callback([=]
        {
            if (hook_set)
            {
                output->render->damage_whole();
            }
        });
    }

    bool toggle()
    {
        if (!wf::get_core().is_gles2())
        {
            return false;
        }

        active = !active;
        if (active && !hook_set)
        {
            output->render->add_post(&render);
            hook_set = true;
        }

        animation.animate(animation, active ? 1 : 0);
        output->render->damage_whole();
        output->render->schedule_redraw();
        return true;
    }

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

        const filter_def *def;
        bool correct;
        select(def, correct);
        wf::gles_texture_t source_tex = wf::gles_texture_t::from_aux(source);

        wf::gles::run_in_context([&]
        {
            wf::gles::bind_render_buffer(dest);
            program.use(wf::TEXTURE_TYPE_RGBA);
            program.attrib_pointer("position", 2, 0, vertexData);
            program.attrib_pointer("uvPosition", 2, 0, coordData);
            program.uniform3f("m0", def->m[0], def->m[1], def->m[2]);
            program.uniform3f("m1", def->m[3], def->m[4], def->m[5]);
            program.uniform3f("m2", def->m[6], def->m[7], def->m[8]);
            program.uniform1f("linearize", def->linear ? 1.f : 0.f);
            program.uniform1f("daltonize", correct ? 1.f : 0.f);
            program.uniform1f("strength", std::clamp(int(strength_opt), 0, 100) / 100.f);
            program.uniform1f("fade", float(double(animation)));
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, source_tex.tex_id));
            GL_CALL(glDisable(GL_BLEND));

            GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

            GL_CALL(glEnable(GL_BLEND));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            program.deactivate();
        });

        if (animation.running())
        {
            /* only while fading: keep redrawing until the fade is done */
            output->render->damage_whole();
            output->render->schedule_redraw();
        } else if (!active)
        {
            hook_set = false;
            output->render->rem_post(&render);
            output->render->damage_whole();
        }
    };

    void fini() override
    {
        active = false;
        if (hook_set)
        {
            output->render->rem_post(&render);
            hook_set = false;
        }

        wf::gles::run_in_context_if_gles([&]
        {
            program.free_resources();
        });
    }
};

/* Global part: key binding + IPC method colorfilter/toggle */
class wayfire_colorfilter_global : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<wayfire_colorfilter_screen>
{
    wf::ipc_activator_t toggle{"colorfilter/toggle"};

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

DECLARE_WAYFIRE_PLUGIN(wayfire_colorfilter_global);
