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
 * Shaders ported from:
 * https://www.shadertoy.com/view/4sd3WB (Buffer A and B)
 * https://www.shadertoy.com/view/Xsd3DB (Image)
 *
 */

#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugins/common/input-grab.hpp>

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

static const char *fragment_shader_a =
    R"(
#version 100
precision highp float;

uniform int num_points;
uniform vec2 points[64];
uniform int button_down;
varying highp vec2 uvpos;
uniform sampler2D u_texture;

void main()
{
    int i;
    for (i = 0; button_down == 1 && i < num_points; i++)
    {
        float d = length(gl_FragCoord.xy - points[i]);
        if (d < 3.)
        {
            gl_FragColor = vec4(0.0, 1.0, 0.0, 0.0);
            return;
        }
    }

    gl_FragColor = texture2D(u_texture, uvpos);
}
)";

static const char *fragment_shader_b =
    R"(
#version 100
precision highp float;

uniform vec2 resolution;
varying highp vec2 uvpos;
uniform sampler2D u_texture;

void main()
{
    float dx = resolution.x;
    float dy = resolution.y;
    vec2 uv = uvpos;

    vec2 udu = texture2D(u_texture, uv).xy;
    // old elevation
    float u = udu.x;
    // old velocity
    float du = udu.y;

    // Finite differences
    float ux = texture2D(u_texture, vec2(uv.x + dx, uv.y)).x;
    float umx = texture2D(u_texture, vec2(uv.x - dx, uv.y)).x;
    float uy = texture2D(u_texture, vec2(uv.x, uv.y + dy)).x;
    float umy = texture2D(u_texture, vec2(uv.x, uv.y - dy)).x;

    // new elevation
    float nu = u + du + 0.28 * (umx + ux + umy + uy - 4.0 * u);
    nu *= 0.99;

    // evaporation
    if (nu < 0.025)
    {
        nu *= 0.2;
    }

    // store elevation and velocity
    gl_FragColor = vec4(nu, nu - u, 0.0, 0.0);
}
)";

static const char *fragment_shader_c =
    R"(
#version 100
precision highp float;

#define DEBUG 0

uniform float fade;
uniform vec2 resolution;
varying highp vec2 uvpos;
uniform sampler2D u_texture;
uniform sampler2D water_texture;

void main()
{
    vec2 uv = uvpos;
#if DEBUG == 1
    float h = texture2D(water_texture, uv).x;
    float sh = 1.35 - h * 2.;
    vec4 effect =
       vec4(exp(pow(sh - .75, 2.) * -10.),
            exp(pow(sh - .50, 2.) * -20.),
            exp(pow(sh - .25, 2.) * -10.),
            1.);
    vec4 fb_pixel = vec4(0.);
    vec4 color = effect;
    if (fade < 1.)
    {
        fb_pixel = texture2D(u_texture, uv) * (1. - fade);
        color *= fade;
        color += fb_pixel;
    }
    gl_FragColor = color;
#else
    vec3 e = vec3(resolution, 0.);
    float p10 = texture2D(water_texture, uv - e.zy).x;
    float p01 = texture2D(water_texture, uv - e.xz).x;
    float p21 = texture2D(water_texture, uv + e.xz).x;
    float p12 = texture2D(water_texture, uv + e.zy).x;

    vec3 grad = normalize(vec3(p21 - p01, p12 - p10, 1.));
    vec4 c = texture2D(u_texture, uv + grad.xy * .35);
    vec3 light = normalize(vec3(.2, -.5, .7));
    float diffuse = dot(grad, light);
    if (diffuse > 0.75)
    {
        diffuse = 1.0;
    }
    float spec = pow(max(0., -reflect(light, grad).z), 32.);
    c = c * diffuse + spec;

    if (fade < 1.)
    {
        vec4 fb_pixel = texture2D(u_texture, uv) * (1. - fade);
        c = c * fade + fb_pixel;
    }

    gl_FragColor = c;
#endif
}
)";

class wayfire_water_screen : public wf::per_output_plugin_instance_t, public wf::pointer_interaction_t
{
    wf::option_wrapper_t<wf::buttonbinding_t> button{"water/activate"};
    wf::animation::simple_animation_t animation =
        wf::animation::simple_animation_t(wf::create_option<int>(5000));
    OpenGL::program_t program[3];
    wf::auxilliary_buffer_t buffer[2];
    wf::pointf_t last_cursor;
    bool button_down = false;
    bool hook_set    = false;
    wf::wl_timer<false> timer;
    int points_loc;
    std::unique_ptr<wf::input_grab_t> input_grab;
    wf::plugin_activation_data_t grab_interface{
        .name = "water",
        .capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR,
    };

  public:
    void init() override
    {
        if (!wf::get_core().is_gles2())
        {
            LOGE("water plugin requires GLES2 renderer!");
            return;
        }

        wf::gles::run_in_context_if_gles([&]
        {
            program[0].set_simple(
                OpenGL::compile_program(vertex_shader, fragment_shader_a));
            program[1].set_simple(
                OpenGL::compile_program(vertex_shader, fragment_shader_b));
            program[2].set_simple(
                OpenGL::compile_program(vertex_shader, fragment_shader_c));
            points_loc = GL_CALL(glGetUniformLocation(
                program[0].get_program_id(wf::TEXTURE_TYPE_RGBA), "points"));
        });

        input_grab = std::make_unique<wf::input_grab_t>(this->grab_interface.name, output, nullptr, this,
            nullptr);

        output->add_button(button, &activate_binding);

        animation.set(0, 0);
    }

    void handle_pointer_button(const wlr_pointer_button_event& event) override
    {
        if (event.state == WL_POINTER_BUTTON_STATE_RELEASED)
        {
            output->deactivate_plugin(&grab_interface);
            timer.set_timeout(5000, timeout);
            input_grab->ungrab_input();
            button_down = false;
        }
    }

    wf::button_callback activate_binding = [=] (auto)
    {
        if (!output->is_plugin_active(grab_interface.name))
        {
            if (!output->activate_plugin(&grab_interface))
            {
                return false;
            }
        }

        if (!hook_set)
        {
            output->render->add_effect(&damage_hook, wf::OUTPUT_EFFECT_DAMAGE);
            output->render->add_post(&render);
            hook_set = true;
        }

        last_cursor = output->get_cursor_position();
        animation.animate(animation, 1);
        input_grab->grab_input(wf::scene::layer::OVERLAY);
        input_grab->set_wants_raw_input(true);
        timer.disconnect();
        button_down = true;

        return false;
    };

    wf::wl_timer<false>::callback_t timeout = [=] ()
    {
        animation.animate(animation, 0);
    };

    wf::effect_hook_t damage_hook = [=] ()
    {
        output->render->damage_whole();
    };

    wf::post_hook_t render = [=] (wf::auxilliary_buffer_t& source, const wf::render_buffer_t& dest)
    {
        auto transform = get_output_matrix_from_transform(output->handle->transform);
        auto cursor_position = output->get_cursor_position();
        auto og  = output->get_relative_geometry();
        auto fbg = output->render->get_target_framebuffer().framebuffer_box_from_geometry_box(og);
        transform = glm::inverse(transform);

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

        wf::pointf_t step;
        std::vector<float> points;
        wf::pointf_t center{0.5, 0.5};
        auto d = glm::distance(glm::vec2(last_cursor.x, last_cursor.y),
            glm::vec2(cursor_position.x, cursor_position.y));

        /* Interpolate between last and current cursor */
        int num_points = std::min(int(d / 5 + 1), 64);
        step.x = (cursor_position.x - last_cursor.x) / num_points;
        step.y = (cursor_position.y - last_cursor.y) / num_points;
        for (int i = 0; i < num_points; i++)
        {
            wf::pointf_t p = wf::pointf_t{
                cursor_position.x - step.x * i,
                cursor_position.y - step.y * i};

            float x = p.x / og.width;
            float y = p.y / og.height;

            /* Apply transform to cursor position */
            glm::vec4 point{x - center.x, y - center.y, 1.0, 1.0};
            glm::vec4 result = transform * point;
            x = (result.x + center.x) * fbg.width;
            y = (result.y + center.y) * fbg.height;
            points.push_back(x);
            points.push_back(y);
        }

        last_cursor = cursor_position;

        /* First pass */

        for (size_t i = 0; i < 2; i++)
        {
            if (buffer[i].allocate(wf::dimensions(fbg)) == wf::buffer_reallocation_result_t::REALLOCATED)
            {
                wf::gles::run_in_context([&]
                {
                    wf::gles::bind_render_buffer(buffer[i].get_renderbuffer());
                    OpenGL::clear({0, 0, 0, 1});
                });
            }
        }

        wf::gles_texture_t tex[2] = {
            wf::gles_texture_t::from_aux(buffer[0]),
            wf::gles_texture_t::from_aux(buffer[1]),
        };
        wf::gles_texture_t source_tex = wf::gles_texture_t::from_aux(source);

        wf::gles::run_in_context([&]
        {
            wf::gles::bind_render_buffer(buffer[0].get_renderbuffer());

            program[0].use(wf::TEXTURE_TYPE_RGBA);
            program[0].attrib_pointer("position", 2, 0, vertexData);
            program[0].attrib_pointer("uvPosition", 2, 0, coordData);
            GL_CALL(glUniform2fv(points_loc, num_points, points.data()));
            program[0].uniform1i("num_points", num_points);
            program[0].uniform1i("button_down", button_down ? 1 : 0);
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, tex[1].tex_id));
            GL_CALL(glDisable(GL_BLEND));

            GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            program[0].deactivate();

            /* Second pass */
            wf::gles::bind_render_buffer(buffer[1].get_renderbuffer());
            program[1].use(wf::TEXTURE_TYPE_RGBA);
            program[1].attrib_pointer("position", 2, 0, vertexData);
            program[1].attrib_pointer("uvPosition", 2, 0, coordData);
            program[1].uniform2f("resolution", 1.0 / fbg.width, 1.0 / fbg.height);
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, tex[0].tex_id));

            GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            program[1].deactivate();

            /* Final pass */
            wf::gles::bind_render_buffer(dest);
            program[2].use(wf::TEXTURE_TYPE_RGBA);
            program[2].attrib_pointer("position", 2, 0, vertexData);
            program[2].attrib_pointer("uvPosition", 2, 0, coordData);
            program[2].uniform2f("resolution", 1.0 / fbg.width, 1.0 / fbg.height);
            program[2].uniform1f("fade", animation);
            program[2].uniform1i("water_texture", 1);
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, source_tex.tex_id));
            GL_CALL(glActiveTexture(GL_TEXTURE0 + 1));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, tex[1].tex_id));

            GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

            GL_CALL(glEnable(GL_BLEND));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            program[2].deactivate();
        });

        if (!button_down && !timer.is_connected() && !animation.running())
        {
            hook_set = false;
            output->render->rem_effect(&damage_hook);
            output->render->rem_post(&render);
            buffer[0].free();
            buffer[1].free();
        }

        output->render->schedule_redraw();
    };

    void fini() override
    {
        output->deactivate_plugin(&grab_interface);
        output->rem_binding(&activate_binding);
        input_grab->ungrab_input();
        timer.disconnect();
        if (hook_set)
        {
            output->render->rem_effect(&damage_hook);
            output->render->rem_post(&render);
        }

        wf::gles::run_in_context([&]
        {
            buffer[0].free();
            buffer[1].free();
            program[0].free_resources();
            program[1].free_resources();
            program[2].free_resources();
        });
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_water_screen>);
