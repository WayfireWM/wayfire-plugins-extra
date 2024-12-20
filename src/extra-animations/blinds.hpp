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


static const char *blinds_vert_source =
    R"(
#version 100

attribute mediump vec3 position;
attribute mediump vec2 uv_in;

uniform mat4 matrix;

varying highp vec2 uv;

void main() {
    uv = uv_in;
    gl_Position = matrix * vec4(position, 1.0);
}
)";

static const char *blinds_frag_source =
    R"(
#version 100
@builtin_ext@
@builtin@

precision mediump float;

varying highp vec2 uv;

void main()
{
    gl_FragColor = get_pixel(uv);
}
)";

namespace wf
{
namespace blinds
{
using namespace wf::scene;
using namespace wf::animate;
using namespace wf::animation;

static std::string blinds_transformer_name = "animation-blinds";

wf::option_wrapper_t<wf::animation_description_t> blinds_duration{"extra-animations/blinds_duration"};
wf::option_wrapper_t<int> blinds_strip_height{"extra-animations/blinds_strip_height"};

class blinds_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
};
class blinds_transformer : public wf::scene::view_2d_transformer_t
{
  public:
    wayfire_view view;
    OpenGL::program_t program;
    wf::output_t *output;
    wf::geometry_t animation_geometry;
    blinds_animation_t progression{blinds_duration};

    class simple_node_render_instance_t : public wf::scene::transformer_render_instance_t<transformer_base_node_t>
    {
        wf::signal::connection_t<node_damage_signal> on_node_damaged =
            [=] (node_damage_signal *ev)
        {
            push_to_parent(ev->region);
        };

        blinds_transformer *self;
        wayfire_view view;
        damage_callback push_to_parent;

      public:
        simple_node_render_instance_t(blinds_transformer *self, damage_callback push_damage,
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

        void render(const wf::render_target_t& target,
            const wf::region_t& region) override
        {
            auto src_box = self->get_children_bounding_box();
            auto src_tex = wf::scene::transformer_render_instance_t<transformer_base_node_t>::get_texture(
                1.0);
            auto progress = self->progression.progress();
            self->animation_geometry =
                wf::geometry_t{src_box.x - int(blinds_strip_height), src_box.y,
                (src_box.x - int(blinds_strip_height)) + src_box.width + int(blinds_strip_height) * 2,
                src_box.y + src_box.height};

            int line_height = int(blinds_strip_height);
            for (int i = 0; i < src_box.height; i += line_height)
            {
                std::vector<float> uv;
                std::vector<float> vertices;
                auto y     = src_box.height - i;
                auto inv_h = 1.0 / src_box.height;
                uv.push_back(1.0);
                uv.push_back(std::max(0, y - line_height) * inv_h);
                uv.push_back(0.0);
                uv.push_back(std::max(0, y - line_height) * inv_h);
                uv.push_back(0.0);
                uv.push_back(y * inv_h);
                uv.push_back(1.0);
                uv.push_back(y * inv_h);
                auto x1 = src_box.width / 2.0;
                auto x2 = -(src_box.width / 2.0);
                auto y1 = -(std::min(src_box.height - i, line_height) / 2.0);
                auto y2 = std::min(src_box.height - i, line_height) / 2.0;
                glm::vec4 v, r;
                glm::mat4 m(1.0);
                m =
                    glm::rotate(m,
                        float(std::min(M_PI,
                            std::max(0.0,
                                (M_PI * (1.0 - progress)) - M_PI / 2.0 * (float(i) / src_box.height)) +
                            M_PI / 2.0)), glm::vec3(1.0, 0.0, 0.0));
                m = glm::scale(m, glm::vec3(2.0f / (src_box.width + line_height * 2), 2.0f / (y2 - y1), 1.0));
                v = glm::vec4(x1, y2, 0.0, 1.0);
                r = m * v;
                vertices.push_back(r.x);
                vertices.push_back(r.y);
                vertices.push_back(r.z);
                v = glm::vec4(x2, y2, 0.0, 1.0);
                r = m * v;
                vertices.push_back(r.x);
                vertices.push_back(r.y);
                vertices.push_back(r.z);
                v = glm::vec4(x2, y1, 0.0, 1.0);
                r = m * v;
                vertices.push_back(r.x);
                vertices.push_back(r.y);
                vertices.push_back(r.z);
                v = glm::vec4(x1, y1, 0.0, 1.0);
                r = m * v;
                vertices.push_back(r.x);
                vertices.push_back(r.y);
                vertices.push_back(r.z);

                glm::mat4 p = glm::perspective(float(M_PI / 64.0), 1.0f, 0.1f, 100.0f);
                glm::mat4 l = glm::lookAt(
                    glm::vec3(0., 0., 1.0 / std::tan(float(M_PI / 64.0) / 2)),
                    glm::vec3(0., 0., 0.),
                    glm::vec3(0., 1., 0.));

                auto transform = p * l;
                wf::render_target_t slice;
                slice.allocate(src_box.width + line_height * 2, y2 - y1);
                OpenGL::render_begin(slice);
                OpenGL::clear(wf::color_t{0.0, 0.0, 0.0, 0.0}, GL_COLOR_BUFFER_BIT);
                self->program.use(wf::TEXTURE_TYPE_RGBA);
                self->program.uniformMatrix4f("matrix", transform);
                self->program.attrib_pointer("position", 3, 0, vertices.data());
                self->program.attrib_pointer("uv_in", 2, 0, uv.data());
                self->program.set_active_texture(src_tex);
                GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, vertices.size() / 3));
                OpenGL::render_end();
                OpenGL::render_begin(target);
                for (auto box : region)
                {
                    target.logic_scissor(wlr_box_from_pixman_box(box));
                    OpenGL::render_transformed_texture(slice.tex, gl_geometry{float(src_box.x - line_height),
                        float(src_box.y + i),
                        float((src_box.x - line_height) + src_box.width + line_height * 2.0),
                        float((src_box.y + i) + (y2 - y1))}, {},
                        target.get_orthographic_projection(), glm::vec4(1.0), 0);
                }

                slice.release();
                OpenGL::render_end();
            }
        }
    };

    blinds_transformer(wayfire_view view, wf::geometry_t bbox) : wf::scene::view_2d_transformer_t(view)
    {
        this->view = view;
        if (view->get_output())
        {
            output = view->get_output();
            output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        }

        animation_geometry =
            wf::geometry_t{bbox.x - int(blinds_strip_height), bbox.y,
            (bbox.x - int(blinds_strip_height)) + bbox.width + int(blinds_strip_height) * 2,
            bbox.y + bbox.height};
        OpenGL::render_begin();
        program.compile(blinds_vert_source, blinds_frag_source);
        OpenGL::render_end();
    }

    wf::geometry_t get_bounding_box() override
    {
        return this->animation_geometry;
    }

    wf::effect_hook_t pre_hook = [=] ()
    {
        output->render->damage(animation_geometry);
        output->render->damage_whole();
    };

    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override
    {
        instances.push_back(std::make_unique<simple_node_render_instance_t>(
            this, push_damage, view));
    }

    void init_animation(bool blinds)
    {
        if (!blinds)
        {
            this->progression.reverse();
        }

        this->progression.start();
    }

    virtual ~blinds_transformer()
    {
        if (output)
        {
            output->render->rem_effect(&pre_hook);
        }

        program.free_resources();
    }
};

class blinds_animation : public animation_base_t
{
    wayfire_view view;

  public:
    void init(wayfire_view view, wf::animation_description_t dur, animation_type type) override
    {
        this->view = view;
        pop_transformer(view);
        auto bbox = view->get_transformed_node()->get_bounding_box();
        auto tmgr = view->get_transformed_node();
        auto node = std::make_shared<wf::blinds::blinds_transformer>(view, bbox);
        tmgr->add_transformer(node, wf::TRANSFORMER_HIGHLEVEL + 1, blinds_transformer_name);
        node->init_animation(type & WF_ANIMATE_HIDING_ANIMATION);
    }

    void pop_transformer(wayfire_view view)
    {
        if (view->get_transformed_node()->get_transformer(blinds_transformer_name))
        {
            view->get_transformed_node()->rem_transformer(blinds_transformer_name);
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

        if (auto tr = tmgr->get_transformer<wf::blinds::blinds_transformer>(blinds_transformer_name))
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
                view->get_transformed_node()->get_transformer<wf::blinds::blinds_transformer>(
                    blinds_transformer_name))
        {
            tr->progression.reverse();
        }
    }
};
}
}
