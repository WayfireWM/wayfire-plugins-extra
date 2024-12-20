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

#include <ctime>
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
#include <boost/polygon/segment_data.hpp>
#include <boost/polygon/voronoi.hpp>

using boost::polygon::voronoi_builder;
using boost::polygon::voronoi_diagram;

static const char *shatter_vert_source =
    R"(
#version 100

attribute mediump vec2 position;
attribute mediump vec2 uv_in;

uniform mat4 matrix;

varying highp vec2 uv;

void main() {
    uv = uv_in;
    gl_Position = matrix * vec4(position, 0.0, 1.0);
}
)";

static const char *shatter_frag_source =
    R"(
#version 100
@builtin_ext@
@builtin@

precision mediump float;

varying highp vec2 uv;
uniform mediump float alpha;

void main()
{
    vec4 pixel = get_pixel(uv);
    gl_FragColor = vec4(pixel * alpha);
}
)";

namespace wf
{
namespace shatter
{
using namespace wf::scene;
using namespace wf::animate;
using namespace wf::animation;

static std::string shatter_transformer_name = "animation-shatter";

wf::option_wrapper_t<wf::animation_description_t> shatter_duration{"extra-animations/shatter_duration"};

class shatter_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
    timed_transition_t shatter{*this};
};
class shatter_transformer : public wf::scene::view_2d_transformer_t
{
  public:
    wayfire_view view;
    OpenGL::program_t program;
    wf::output_t *output;
    wf::geometry_t animation_geometry;
    shatter_animation_t progression{shatter_duration};
    voronoi_diagram<double> vd;
    std::vector<glm::vec3> rotations;
    std::vector<boost::polygon::point_data<int>> points;

    class simple_node_render_instance_t : public wf::scene::transformer_render_instance_t<transformer_base_node_t>
    {
        wf::signal::connection_t<node_damage_signal> on_node_damaged =
            [=] (node_damage_signal *ev)
        {
            push_to_parent(ev->region);
        };

        shatter_transformer *self;
        wayfire_view view;
        damage_callback push_to_parent;

      public:
        simple_node_render_instance_t(shatter_transformer *self, damage_callback push_damage,
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
                        .damage   = damage // & self->get_bounding_box(),
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
            auto progress_pt_one = (std::clamp(progress, 0.5, 1.0) - 0.5) * 2.0;
            auto progress_pt_two = std::clamp(progress, 0.0, 0.5) * 2.0;
            auto og = self->output->get_relative_geometry();

            OpenGL::render_begin(target);
            GL_CALL(glDisable(GL_CULL_FACE));
            GL_CALL(glEnable(GL_BLEND));
            GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));
            self->program.use(wf::TEXTURE_TYPE_RGBA);
            self->program.set_active_texture(src_tex);
            int i = 0;
            glm::mat4 l = glm::lookAt(
                glm::vec3(0., 0., 1.0 / std::tan(float(M_PI / 4.0) / 2)),
                glm::vec3(0., 0., 0.),
                glm::vec3(0., 1., 0.));
            glm::mat4 p = glm::perspective(float(M_PI / 4.0), 1.0f, 0.1f, 100.0f);
            for (voronoi_diagram<double>::const_cell_iterator cell = self->vd.cells().begin();
                 cell != self->vd.cells().end();
                 cell++, i++)
            {
                const boost::polygon::voronoi_edge<double> *edge = cell->incident_edge();
                std::vector<float> uv;
                std::vector<float> vertices;
                // bounding box of polygon
                float x1 = std::numeric_limits<float>::max();
                float y1 = std::numeric_limits<float>::max();
                float x2 = std::numeric_limits<float>::min();
                float y2 = std::numeric_limits<float>::min();
                if (!edge)
                {
                    continue;
                }

                do {
                    if (!edge)
                    {
                        continue;
                    }

                    edge = edge->next();
                    if (edge && edge->vertex0() && !std::isnan(edge->vertex0()->x()) &&
                        !std::isnan(edge->vertex0()->y()))
                    {
                        auto x = std::clamp(edge->vertex0()->x(), double(0.0), double(src_box.width));
                        auto y = std::clamp(edge->vertex0()->y(), double(0.0), double(src_box.height));
                        uv.push_back(x / src_box.width);
                        uv.push_back(y / src_box.height);
                        if (x1 > x)
                        {
                            x1 = x;
                        }

                        if (y1 > y)
                        {
                            y1 = y;
                        }

                        if (x2 < x)
                        {
                            x2 = x;
                        }

                        if (y2 < y)
                        {
                            y2 = y;
                        }
                    }
                } while (edge != cell->incident_edge());

                auto center = glm::vec2(x1 + (x2 - x1) / 2.0f, y1 + (y2 - y1) / 2.0f);
                do {
                    if (!edge)
                    {
                        continue;
                    }

                    edge = edge->next();
                    if (edge && edge->vertex0() && !std::isnan(edge->vertex0()->x()) &&
                        !std::isnan(edge->vertex0()->y()))
                    {
                        auto x = std::clamp(edge->vertex0()->x(), double(0.0), double(src_box.width));
                        auto y = std::clamp(edge->vertex0()->y(), double(0.0), double(src_box.height));
                        glm::vec4 v, r;
                        glm::mat4 m(1.0);
                        m =
                            glm::rotate(m,
                                float(progress_pt_one * progress_pt_one * self->rotations.data()[i].z), glm::vec3(
                                    0.0, 0.0,
                                    1.0));
                        m = glm::scale(m, glm::vec3(2.0f / og.width, 2.0f / og.height, 1.0));
                        m = glm::translate(m, glm::vec3(-(center.x), -(center.y), 0.0));
                        v = glm::vec4(x, y, 0.0, 1.0);
                        r = m * v;
                        vertices.push_back(r.x);
                        vertices.push_back(r.y);
                    }
                } while (edge != cell->incident_edge());

                glm::mat4 m(1.0);
                m = glm::translate(m, glm::vec3(
                    ((progress_pt_one * progress_pt_one + progress_pt_two * 0.01) *
                        (center.x - src_box.width / 2.0f) * self->rotations.data()[i].x) * (2.0f / og.width),
                    ((progress_pt_one * progress_pt_one + progress_pt_two * 0.01) *
                        (center.y - src_box.height / 2.0f) * self->rotations.data()[i].y) * (2.0f / og.width),
                    (progress_pt_one * progress_pt_one * self->rotations.data()[i].z) * (2.0f / og.width)));
                m = glm::translate(m, glm::vec3(
                    ((center.x - og.width / 2.0f) + src_box.x) * (2.0f / og.width),
                    ((center.y - og.height / 2.0f) + (og.height - src_box.y - src_box.height)) *
                    (2.0f / og.height), 0.0));
                auto alpha = std::clamp((1.0 - progress) * 2.0, 0.0, 1.0);
                self->program.uniformMatrix4f("matrix", target.transform * m * p * l);
                self->program.uniform1f("alpha", alpha);
                self->program.attrib_pointer("position", 2, 0, vertices.data());
                self->program.attrib_pointer("uv_in", 2, 0, uv.data());
                if (vertices.size() / 2 >= 3)
                {
                    GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, vertices.size() / 2));
                }
            }

            self->program.deactivate();
            OpenGL::render_end();
        }
    };

    shatter_transformer(wayfire_view view, wf::geometry_t bbox) : wf::scene::view_2d_transformer_t(view)
    {
        this->view = view;
        if (view->get_output())
        {
            output = view->get_output();
            output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        }

        auto og = output->get_relative_geometry();
        animation_geometry = og;
        OpenGL::render_begin();
        program.compile(shatter_vert_source, shatter_frag_source);
        OpenGL::render_end();

        std::srand(std::time(nullptr));
        auto offset_range = 100;
        for (int y = -offset_range;
             y < bbox.height + offset_range * 2;
             y += int((std::rand() / float(RAND_MAX)) * offset_range))
        {
            for (int x = -offset_range * 1.75f;
                 x < bbox.width + offset_range * 1.75f * 2;
                 x += int((std::rand() / float(RAND_MAX)) * offset_range * 1.75f))
            {
                points.push_back(boost::polygon::point_data<int>(x, y));
                rotations.push_back(glm::vec3(
                    ((std::rand() / float(RAND_MAX)) * 5.0f + 5.0f),
                    ((std::rand() / float(RAND_MAX)) * 5.0f + 5.0f),
                    M_PI * 2.0 * ((std::rand() / float(RAND_MAX)) * 10.0f - 5.0f)));
            }
        }

        construct_voronoi(points.begin(), points.end(), &vd);
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

    void init_animation(bool shatter)
    {
        if (!shatter)
        {
            this->progression.reverse();
        }

        this->progression.start();
    }

    virtual ~shatter_transformer()
    {
        if (output)
        {
            output->render->rem_effect(&pre_hook);
        }

        program.free_resources();
    }
};

class shatter_animation : public animation_base_t
{
    wayfire_view view;

  public:
    void init(wayfire_view view, wf::animation_description_t dur, animation_type type) override
    {
        this->view = view;
        pop_transformer(view);
        auto bbox = view->get_transformed_node()->get_bounding_box();
        auto tmgr = view->get_transformed_node();
        auto node = std::make_shared<shatter_transformer>(view, bbox);
        tmgr->add_transformer(node, wf::TRANSFORMER_HIGHLEVEL + 1, shatter_transformer_name);
        node->init_animation(type & WF_ANIMATE_HIDING_ANIMATION);
    }

    void pop_transformer(wayfire_view view)
    {
        if (view->get_transformed_node()->get_transformer(shatter_transformer_name))
        {
            view->get_transformed_node()->rem_transformer(shatter_transformer_name);
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
                tmgr->get_transformer<shatter_transformer>(shatter_transformer_name))
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
                view->get_transformed_node()->get_transformer<shatter_transformer>(
                    shatter_transformer_name))
        {
            tr->progression.reverse();
        }
    }
};
}
}
