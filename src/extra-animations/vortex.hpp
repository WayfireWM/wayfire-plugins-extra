/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 Scott Moreau <oreaus@gmail.com>
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

#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/core.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/txn/transaction-manager.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/util/duration.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <wayfire/plugins/animate/animate.hpp>
#include <wayfire/util.hpp>


static const char *vortex_vert_source =
    R"(
#version 100

attribute highp vec2 position;
attribute highp vec2 uv_in;

uniform mat4 matrix;

varying highp vec2 uv;

void main() {
    uv = uv_in;
    gl_Position = matrix * vec4(position, 0.0, 1.0);
}
)";

static const char *vortex_frag_source =
    R"(
#version 100
@builtin_ext@
@builtin@

precision highp float;

varying highp vec2 uv;
uniform highp float progress;

const float PI = 3.1415926535897932384626433832795;

vec2 rotate(vec2 uv, float rotation, vec2 mid)
{
    return vec2(
      cos(rotation) * (uv.x - mid.x) + sin(rotation) * (uv.y - mid.y) + mid.x,
      cos(rotation) * (uv.y - mid.y) - sin(rotation) * (uv.x - mid.x) + mid.y
    );
}

void main()
{
    vec2 uv_vortex;
    float intensity = 50.0;
    vec2 center = vec2(0.5, 0.5);
    float d = distance(uv, center);
    float progress_pt_one = clamp(progress, 0.0, 0.5) * 2.0;
    float progress_pt_two = (clamp(progress, 0.5, 1.0) - 0.5) * 2.0;
    float sigmoid = 1.0 / (1.0 + pow(2.718, -(d * 12.0)));
    vec2 r = uv - (center - uv) * progress_pt_two * progress_pt_two * 5.0;
    r -= (center - r) * progress_pt_one * progress_pt_one * (2.0 - (sigmoid - 0.5) * 4.0);
    uv_vortex = rotate(r, (1.0 - (sigmoid - 0.5) * 2.0) * progress * progress * intensity, center);

    if (uv_vortex.x < 0.0 || uv_vortex.y < 0.0 ||
        uv_vortex.x > 1.0 || uv_vortex.y > 1.0)
    {
        discard;
    }

    gl_FragColor = get_pixel(uv_vortex) * clamp(1.0 - progress, 0.0, 0.25) * 4.0;
}
)";

namespace wf
{
namespace vortex
{
using namespace wf::scene;
using namespace wf::animate;
using namespace wf::animation;

static std::string vortex_transformer_name = "animation-vortex";

class vortex_transformer : public wf::scene::view_2d_transformer_t
{
  public:
    wayfire_view view;
    OpenGL::program_t program;
    wf::output_t *output;
    wf::geometry_t animation_geometry;
    duration_t progression;

    class simple_node_render_instance_t : public wf::scene::transformer_render_instance_t<transformer_base_node_t>
    {
        wf::signal::connection_t<node_damage_signal> on_node_damaged =
            [=] (node_damage_signal *ev)
        {
            push_to_parent(ev->region);
        };

        vortex_transformer *self;
        wayfire_view view;
        damage_callback push_to_parent;

      public:
        simple_node_render_instance_t(vortex_transformer *self, damage_callback push_damage,
            wayfire_view view) : wf::scene::transformer_render_instance_t<transformer_base_node_t>(self,
                push_damage,
                view->get_output())
        {
            this->self = self;
            this->view = view;
            this->push_to_parent = push_damage;
            self->connect(&on_node_damaged);
        }

        ~simple_node_render_instance_t()
        {}

        void schedule_instructions(
            std::vector<render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::region_t& damage) override
        {
            instructions.push_back(render_instruction_t{
                        .instance = this,
                        .target   = target,
                        .damage   = damage & self->animation_geometry,
                    });
        }

        void transform_damage_region(wf::region_t& damage) override
        {
            damage |= wf::region_t{self->animation_geometry};
        }

        void render(const wf::scene::render_instruction_t& data) override
        {
            auto src_box  = self->get_children_bounding_box();
            auto src_tex  = get_texture(1.0);
            auto gl_tex   = wf::gles_texture_t{src_tex};
            auto progress = self->progression.progress();
            static const float vertex_data_uv[] = {
                0.0f, 0.0f,
                1.0f, 0.0f,
                1.0f, 1.0f,
                0.0f, 1.0f,
            };

            const float vertex_data_pos[] = {
                1.0f * src_box.x,
                1.0f * src_box.y + src_box.height,
                1.0f * src_box.x + src_box.width,
                1.0f * src_box.y + src_box.height,
                1.0f * src_box.x + src_box.width,
                1.0f * src_box.y,
                1.0f * src_box.x, 1.0f * src_box.y,
            };

            data.pass->custom_gles_subpass([&]
            {
                wf::gles::bind_render_buffer(data.target);
                self->program.use(wf::TEXTURE_TYPE_RGBA);
                self->program.uniformMatrix4f("matrix",
                    wf::gles::render_target_orthographic_projection(data.target));
                self->program.attrib_pointer("position", 2, 0, vertex_data_pos);
                self->program.attrib_pointer("uv_in", 2, 0, vertex_data_uv);
                self->program.uniform1f("progress", progress);
                self->program.set_active_texture(gl_tex);
                GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
            });
        }
    };

    vortex_transformer(wayfire_view view, wf::geometry_t bbox,
        wf::animation_description_t duration) : wf::scene::view_2d_transformer_t(view)
    {
        this->view = view;
        this->progression = duration_t{wf::create_option(duration)};
        if (view->get_output())
        {
            output = view->get_output();
            output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        }

        animation_geometry = bbox;
        wf::gles::run_in_context([&]
        {
            program.compile(vortex_vert_source, vortex_frag_source);
        });
    }

    wf::geometry_t get_bounding_box() override
    {
        return this->animation_geometry;
    }

    wf::effect_hook_t pre_hook = [=] ()
    {
        output->render->damage(animation_geometry);
    };

    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override
    {
        instances.push_back(std::make_unique<simple_node_render_instance_t>(
            this, push_damage, view));
    }

    void init_animation(bool vortex)
    {
        if (!vortex)
        {
            this->progression.reverse();
        }

        this->progression.start();
    }

    virtual ~vortex_transformer()
    {
        if (output)
        {
            output->render->rem_effect(&pre_hook);
        }

        wf::gles::run_in_context_if_gles([&]
        {
            program.free_resources();
        });
    }
};

class vortex_animation : public animation_base_t
{
    wayfire_view view;

  public:
    void init(wayfire_view view, wf::animation_description_t dur, animation_type type) override
    {
        this->view = view;
        pop_transformer(view);
        auto bbox = view->get_transformed_node()->get_bounding_box();
        auto tmgr = view->get_transformed_node();
        auto node = std::make_shared<vortex_transformer>(view, bbox, dur);
        tmgr->add_transformer(node, wf::TRANSFORMER_HIGHLEVEL + 1, vortex_transformer_name);
        node->init_animation(type & WF_ANIMATE_HIDING_ANIMATION);
    }

    void pop_transformer(wayfire_view view)
    {
        if (view->get_transformed_node()->get_transformer(vortex_transformer_name))
        {
            view->get_transformed_node()->rem_transformer(vortex_transformer_name);
        }
    }

    bool step() override
    {
        if (!view)
        {
            return false;
        }

        auto tmgr = view->get_transformed_node();
        if (!tmgr)
        {
            return false;
        }

        if (auto tr =
                tmgr->get_transformer<wf::vortex::vortex_transformer>(vortex_transformer_name))
        {
            auto running = tr->progression.running();
            if (!running)
            {
                pop_transformer(view);
                return false;
            }

            return running;
        }

        return false;
    }

    void reverse() override
    {
        if (auto tr =
                view->get_transformed_node()->get_transformer<wf::vortex::vortex_transformer>(
                    vortex_transformer_name))
        {
            tr->progression.reverse();
        }
    }
};
}
}
