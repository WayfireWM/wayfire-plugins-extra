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
uniform float fade0;
uniform float fade1;
uniform float fade2;
uniform float fade3;
uniform float fade4;
uniform float fade_center;
uniform vec4 finger_color;
uniform vec4 center_color;
uniform float radius;

void main()
{
    vec4 c = get_pixel(uvpos);
    float m = distance(uvpos * resolution, finger0);
    if (m < radius)
        c = mix(finger_color * fade0, c, m / (radius * 2.0));
    m = distance(uvpos * resolution, finger1);
    if (m < radius)
        c = mix(finger_color * fade1, c, m / (radius * 2.0));
    m = distance(uvpos * resolution, finger2);
    if (m < radius)
        c = mix(finger_color * fade2, c, m / (radius * 2.0));
    m = distance(uvpos * resolution, finger3);
    if (m < radius)
        c = mix(finger_color * fade3, c, m / (radius * 2.0));
    m = distance(uvpos * resolution, finger4);
    if (m < radius)
        c = mix(finger_color * fade4, c, m / (radius * 2.0));
    m = distance(uvpos * resolution, center);
    if (m < radius)
        c = mix(center_color * fade_center, c, m / (radius * 2.0));
    out_color = c;
}
)";

class wayfire_showtouch : public wf::per_output_plugin_instance_t
{
    bool hook_set = false;
    wf::pointf_t points[6];
    wf::option_wrapper_t<wf::color_t> finger_color{"showtouch/finger_color"};
    wf::option_wrapper_t<wf::color_t> center_color{"showtouch/center_color"};
    wf::option_wrapper_t<int> touch_radius{"showtouch/touch_radius"};
    wf::option_wrapper_t<wf::animation_description_t> touch_duration{"showtouch/touch_duration"};

    OpenGL::program_t program;
    wf::animation::simple_animation_t fade0{touch_duration};
    wf::animation::simple_animation_t fade1{touch_duration};
    wf::animation::simple_animation_t fade2{touch_duration};
    wf::animation::simple_animation_t fade3{touch_duration};
    wf::animation::simple_animation_t fade4{touch_duration};
    wf::animation::simple_animation_t fade_center{touch_duration};

  public:
    void init() override
    {
        OpenGL::render_begin();
        program.compile(vertex_shader, fragment_shader);
        OpenGL::render_end();
        wf::get_core().connect(&on_touch_down);
        wf::get_core().connect(&on_touch_up);
        fade0.set(0.0, 0.0);
        fade1.set(0.0, 0.0);
        fade2.set(0.0, 0.0);
        fade3.set(0.0, 0.0);
        fade4.set(0.0, 0.0);
        fade_center.set(0.0, 0.0);
        points[0] = {-100, -100};
        points[1] = {-100, -100};
        points[2] = {-100, -100};
        points[3] = {-100, -100};
        points[4] = {-100, -100};
        points[5] = {-100, -100};
    }

    void set_hook()
    {
        if (hook_set)
        {
            return;
        }

        output->render->add_post(&post_hook);
        output->render->add_effect(&frame_pre_paint, wf::OUTPUT_EFFECT_DAMAGE);
        output->render->damage_whole();
        hook_set = true;
    }

    void unset_hook()
    {
        if (!hook_set)
        {
            return;
        }

        output->render->rem_post(&post_hook);
        output->render->rem_effect(&frame_pre_paint);
        output->render->damage_whole();
        hook_set = false;
    }

    wf::signal::connection_t<wf::input_event_signal<wlr_touch_down_event>> on_touch_down =
        [=] (wf::input_event_signal<wlr_touch_down_event> *ev)
    {
        switch (ev->event->touch_id)
        {
          case 0:
            fade0.set(1.0, 1.0);
            break;

          case 1:
            fade1.set(1.0, 1.0);
            break;

          case 2:
            fade2.set(1.0, 1.0);
            break;

          case 3:
            fade3.set(1.0, 1.0);
            break;

          case 4:
            fade4.set(1.0, 1.0);
            break;

          default:
            break;
        }

        fade_center.set(1.0, 1.0);
        set_hook();
    };

    wf::signal::connection_t<wf::input_event_signal<wlr_touch_up_event>> on_touch_up =
        [=] (wf::input_event_signal<wlr_touch_up_event> *ev)
    {
        switch (ev->event->touch_id)
        {
          case 0:
            fade0.animate(0.0);
            break;

          case 1:
            fade1.animate(0.0);
            break;

          case 2:
            fade2.animate(0.0);
            break;

          case 3:
            fade3.animate(0.0);
            break;

          case 4:
            fade4.animate(0.0);
            break;

          default:
            break;
        }
    };

    wf::effect_hook_t frame_pre_paint = [=] ()
    {
        if (double(fade0) == 0.0)
        {
            points[0] = {-100, -100};
        }

        if (double(fade1) == 0.0)
        {
            points[1] = {-100, -100};
        }

        if (double(fade2) == 0.0)
        {
            points[2] = {-100, -100};
        }

        if (double(fade3) == 0.0)
        {
            points[3] = {-100, -100};
        }

        if (double(fade4) == 0.0)
        {
            points[4] = {-100, -100};
        }

        if (double(fade_center) == 0.0)
        {
            points[5] = {-100, -100};
        }

        if ((double(fade0) == 0.0) && (double(fade1) == 0.0) && (double(fade2) == 0.0) &&
            (double(fade3) == 0.0) && (double(fade4) == 0.0) && (double(fade_center) == 1.0))
        {
            fade_center.animate(0.0);
        } else if (double(fade_center) == 0.0)
        {
            unset_hook();
        }

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
            switch (i)
            {
              case 0:
                program.uniform1f("fade0", double(fade0));
                break;

              case 1:
                program.uniform1f("fade1", double(fade1));
                break;

              case 2:
                program.uniform1f("fade2", double(fade2));
                break;

              case 3:
                program.uniform1f("fade3", double(fade3));
                break;

              case 4:
                program.uniform1f("fade4", double(fade4));
                break;

              default:
                break;
            }
        }

        program.uniform1f("fade_center", double(fade_center));

        const auto& touch_state = wf::get_core().get_touch_state();
        for (auto& finger : touch_state.fingers)
        {
            auto n = finger.first;
            auto f = finger.second.current;
            switch (n)
            {
              case 0:
                points[0] = {f.x, f.y};
                break;

              case 1:
                points[1] = {f.x, f.y};
                break;

              case 2:
                points[2] = {f.x, f.y};
                break;

              case 3:
                points[3] = {f.x, f.y};
                break;

              case 4:
                points[4] = {f.x, f.y};
                break;

              default:
                break;
            }

            const auto c = touch_state.get_center().current;
            points[5] = {c.x, c.y};
        }

        for (int i = 0; i < 5; i++)
        {
            program.uniform2f("finger" + std::to_string(i), points[i].x, points[i].y);
        }

        program.uniform2f("center", points[5].x, points[5].y);
        program.uniform4f("finger_color", glm::vec4(
            wf::color_t(finger_color).r,
            wf::color_t(finger_color).g,
            wf::color_t(finger_color).b,
            wf::color_t(finger_color).a));
        program.uniform4f("center_color", glm::vec4(
            wf::color_t(center_color).r,
            wf::color_t(center_color).g,
            wf::color_t(center_color).b,
            wf::color_t(center_color).a));
        program.uniform1f("radius", double(touch_radius));
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
        on_touch_up.disconnect();
        on_touch_down.disconnect();
        unset_hook();
        output->render->damage_whole();
        program.free_resources();
    }
};
}
}

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wf::showtouch::wayfire_showtouch>);
