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
 * firepaint: paint fire with the mouse, like Compiz's Firepaint. While the
 * binding (default Shift+Super+left button) is held, fire particles are
 * emitted along the pointer path; each rises, flickers sideways, turns
 * from yellow over orange to red and fades out. Particles are drawn as
 * small additive quads on top of the finished frame (post hook), so the
 * cost depends on the number of particles, not on the screen size.
 * Input handling as in water.cpp.
 */

#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugins/common/input-grab.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <vector>

#define MAX_PARTICLES 6000

static const char *copy_vertex =
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

static const char *copy_fragment =
    R"(
#version 100
precision highp float;
uniform sampler2D u_texture;
varying highp vec2 uvpos;
void main()
{
    gl_FragColor = vec4(texture2D(u_texture, uvpos).rgb, 1.0);
}
)";

static const char *particle_vertex =
    R"(
#version 100
attribute highp vec2 position;   /* NDC */
attribute highp vec2 corner;     /* -1..1 inside the quad */
attribute highp vec4 color;
varying highp vec2 vcorner;
varying highp vec4 vcolor;
void main()
{
    gl_Position = vec4(position.xy, 0.0, 1.0);
    vcorner = corner;
    vcolor  = color;
}
)";

static const char *particle_fragment =
    R"(
#version 100
precision highp float;
varying highp vec2 vcorner;
varying highp vec4 vcolor;
void main()
{
    float d = length(vcorner);
    float a = vcolor.a * (1.0 - smoothstep(0.15, 1.0, d));
    gl_FragColor = vec4(vcolor.rgb * a, 0.0); /* additive: blend ONE, ONE */
}
)";

struct particle_t
{
    float x, y; /* framebuffer pixels, y = 0 at the top */
    float vx, vy; /* pixels per second */
    float life; /* remaining, 1 -> 0 */
    float size; /* radius in pixels */
};

class wayfire_firepaint_screen : public wf::per_output_plugin_instance_t,
    public wf::pointer_interaction_t
{
    wf::option_wrapper_t<wf::buttonbinding_t> button{"firepaint/draw"};
    wf::option_wrapper_t<int> size_opt{"firepaint/size"};
    wf::option_wrapper_t<int> life_opt{"firepaint/life"};
    wf::option_wrapper_t<int> rate_opt{"firepaint/rate"};
    wf::option_wrapper_t<wf::color_t> color_opt{"firepaint/color"};

    OpenGL::program_t copy_program, particle_program;
    std::vector<particle_t> particles;
    std::mt19937 rng{std::random_device{} ()};
    std::chrono::steady_clock::time_point last_frame;
    wf::pointf_t last_cursor;
    double emit_carry = 0.0;
    bool drawing  = false;
    bool hook_set = false;
    std::unique_ptr<wf::input_grab_t> input_grab;
    wf::plugin_activation_data_t grab_interface{
        .name = "firepaint",
        .capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR,
    };

    float rnd(float a, float b)
    {
        return std::uniform_real_distribution<float>(a, b)(rng);
    }

    /* cursor in framebuffer pixels (y = 0 at the top), output transform
     * applied as in water.cpp */
    wf::pointf_t cursor_fb()
    {
        auto og  = output->get_relative_geometry();
        auto fbg = output->render->get_target_framebuffer().framebuffer_box_from_geometry_box(og);
        auto c   = output->get_cursor_position();
        auto transform = glm::inverse(get_output_matrix_from_transform(output->handle->transform));
        glm::vec4 p{c.x / og.width - 0.5, c.y / og.height - 0.5, 1.0, 1.0};
        p = transform * p;
        return {(p.x + 0.5) * fbg.width, (p.y + 0.5) * fbg.height};
    }

    void emit(double dt)
    {
        auto c = cursor_fb();
        emit_carry += dt * std::clamp(int(rate_opt), 10, 2000);
        int n = int(emit_carry);
        emit_carry -= n;
        /* spread the new particles along the path since the last frame */
        float dist = std::hypot(c.x - last_cursor.x, c.y - last_cursor.y);
        n += int(dist / 4.0f);
        float base = std::clamp(int(size_opt), 2, 60);
        for (int i = 0; (i < n) && (particles.size() < MAX_PARTICLES); i++)
        {
            float t = (n > 1) ? float(i) / (n - 1) : 1.0f;
            particle_t p;
            p.x    = last_cursor.x + (c.x - last_cursor.x) * t + rnd(-base * 0.3f, base * 0.3f);
            p.y    = last_cursor.y + (c.y - last_cursor.y) * t + rnd(-base * 0.3f, base * 0.3f);
            p.vx   = rnd(-25.f, 25.f);
            p.vy   = rnd(-90.f, -30.f);
            p.life = rnd(0.7f, 1.0f);
            p.size = base * rnd(0.6f, 1.0f);
            particles.push_back(p);
        }

        last_cursor = c;
    }

    void step(double dt)
    {
        float lifetime = std::clamp(int(life_opt), 200, 10000) / 1000.0f;
        for (auto& p : particles)
        {
            p.vx   += rnd(-200.f, 200.f) * dt; /* flicker */
            p.vy   -= 60.f * dt;               /* hot air rises (y grows downwards) */
            p.x    += p.vx * dt;
            p.y    += p.vy * dt;
            p.life -= dt / lifetime;
        }

        particles.erase(std::remove_if(particles.begin(), particles.end(),
            [] (const particle_t& p) { return p.life <= 0.f; }), particles.end());
    }

    void set_hooks()
    {
        if (!hook_set)
        {
            output->render->add_effect(&damage_hook, wf::OUTPUT_EFFECT_DAMAGE);
            output->render->add_post(&render);
            hook_set   = true;
            last_frame = std::chrono::steady_clock::now();
        }
    }

  public:
    void init() override
    {
        if (!wf::get_core().is_gles2())
        {
            LOGE("firepaint plugin requires GLES2 renderer!");
            return;
        }

        wf::gles::run_in_context_if_gles([&]
        {
            copy_program.set_simple(OpenGL::compile_program(copy_vertex, copy_fragment));
            particle_program.set_simple(OpenGL::compile_program(particle_vertex, particle_fragment));
        });

        input_grab = std::make_unique<wf::input_grab_t>(grab_interface.name, output, nullptr, this, nullptr);
        output->add_button(button, &activate_binding);
    }

    void handle_pointer_button(const wlr_pointer_button_event& event) override
    {
        if (event.state == WL_POINTER_BUTTON_STATE_RELEASED)
        {
            drawing = false;
            input_grab->ungrab_input();
            output->deactivate_plugin(&grab_interface);
        }
    }

    wf::button_callback activate_binding = [=] (auto)
    {
        if (!output->is_plugin_active(grab_interface.name) &&
            !output->activate_plugin(&grab_interface))
        {
            return false;
        }

        set_hooks();
        last_cursor = cursor_fb();
        drawing     = true;
        input_grab->grab_input(wf::scene::layer::OVERLAY);
        return false;
    };

    wf::effect_hook_t damage_hook = [=] ()
    {
        output->render->damage_whole();
    };

    wf::post_hook_t render = [=] (wf::auxilliary_buffer_t& source, const wf::render_buffer_t& dest)
    {
        static const float vertexData[] = {-1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f};
        static const float coordData[]  = {0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        static const float quad[12]     = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};

        auto now  = std::chrono::steady_clock::now();
        double dt = std::clamp(std::chrono::duration<double>(now - last_frame).count(), 0.0, 0.1);
        last_frame = now;
        if (drawing)
        {
            emit(dt);
        }

        step(dt);

        auto og = output->get_relative_geometry();
        auto fbg = output->render->get_target_framebuffer().framebuffer_box_from_geometry_box(og);
        float W = std::max(1, fbg.width), H = std::max(1, fbg.height);
        wf::color_t base = color_opt;

        /* 6 vertices (two triangles) per particle; in the post hook's target
         * NDC y = -1 is the top of the screen, like texture v = 0 */
        std::vector<float> pos, corner, color;
        pos.reserve(particles.size() * 12);
        corner.reserve(particles.size() * 12);
        color.reserve(particles.size() * 24);
        for (auto& p : particles)
        {
            float l = std::max(p.life, 0.f);
            /* young: yellow-white; old: the base colour fading to dark red */
            float r = std::min(1.0, base.r + 0.5 * l);
            float g = std::min(1.0, base.g * (0.4 + 0.9 * l) + 0.35 * l * l);
            float b = std::min(1.0, base.b * l + 0.25 * l * l * l);
            float a = std::min(1.f, l * 1.3f) * 0.8f;
            float s = p.size * (0.5f + 0.5f * l);
            for (int k = 0; k < 6; k++)
            {
                float cx = quad[2 * k], cy = quad[2 * k + 1];
                pos.push_back((p.x + cx * s) / W * 2.f - 1.f);
                pos.push_back((p.y + cy * s) / H * 2.f - 1.f);
                corner.push_back(cx);
                corner.push_back(cy);
                color.insert(color.end(), {r, g, b, a});
            }
        }

        wf::gles_texture_t source_tex = wf::gles_texture_t::from_aux(source);
        wf::gles::run_in_context([&]
        {
            wf::gles::bind_render_buffer(dest);
            /* 1. the frame itself */
            copy_program.use(wf::TEXTURE_TYPE_RGBA);
            copy_program.attrib_pointer("position", 2, 0, vertexData);
            copy_program.attrib_pointer("uvPosition", 2, 0, coordData);
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, source_tex.tex_id));
            GL_CALL(glDisable(GL_BLEND));
            GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            copy_program.deactivate();

            /* 2. the fire, added on top */
            if (!particles.empty())
            {
                particle_program.use(wf::TEXTURE_TYPE_RGBA);
                particle_program.attrib_pointer("position", 2, 0, pos.data());
                particle_program.attrib_pointer("corner", 2, 0, corner.data());
                particle_program.attrib_pointer("color", 4, 0, color.data());
                GL_CALL(glEnable(GL_BLEND));
                GL_CALL(glBlendFunc(GL_ONE, GL_ONE));
                GL_CALL(glDrawArrays(GL_TRIANGLES, 0, GLsizei(particles.size() * 6)));
                GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
                particle_program.deactivate();
            }

            GL_CALL(glEnable(GL_BLEND));
        });

        if (!drawing && particles.empty())
        {
            hook_set = false;
            output->render->rem_effect(&damage_hook);
            output->render->rem_post(&render);
        }

        output->render->schedule_redraw();
    };

    void fini() override
    {
        drawing = false;
        output->rem_binding(&activate_binding);
        if (input_grab)
        {
            input_grab->ungrab_input();
        }

        output->deactivate_plugin(&grab_interface);
        if (hook_set)
        {
            output->render->rem_effect(&damage_hook);
            output->render->rem_post(&render);
            hook_set = false;
        }

        wf::gles::run_in_context_if_gles([&]
        {
            copy_program.free_resources();
            particle_program.free_resources();
        });
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_firepaint_screen>);
