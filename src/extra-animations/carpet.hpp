/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 Scott Moreau <oreaus@gmail.com>
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

#include <wayfire/core.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/plugins/animate/animate.hpp>


static const char *carpet_vert_source =
    R"(
#version 100

attribute highp vec2 position;
attribute highp vec2 uv_in;

varying highp vec2 uvpos_var;

void main() {

    gl_Position = vec4(position.xy, 0.0, 1.0);
    uvpos_var = uv_in;
}
)";

static const char *carpet_frag_source =
    R"(
#version 100
@builtin_ext@
@builtin@

precision highp float;

varying highp vec2 uvpos_var;

uniform float progress;
uniform int direction;

#define M_PI 3.1415926535897932384626433832795

void main()
{
    vec4 wfrag;
    vec2 uv;
    vec2 uvpos;
    if (direction == 1) // right
    {
        uvpos = vec2(1.0 - uvpos_var.x, uvpos_var.y);
    } else if (direction == 2) // top
    {
        uvpos = vec2(1.0 - uvpos_var.y, uvpos_var.x);
    } else if (direction == 3) // bottom
    {
        uvpos = vec2(uvpos_var.y, uvpos_var.x);
    } else // left
    {
        uvpos = uvpos_var;
    }
    float offset = 0.1;
    float p = progress * 1.2 - 0.2;

    // initial color (transparent)
    wfrag = vec4(0.0);
    // get pixel from default position if left of peel line
    if (uvpos.x < p + offset + 0.05)
    {
        if (uvpos_var.x >= 0.0 && uvpos_var.x <= 1.0 &&
            uvpos_var.y >= 0.0 && uvpos_var.y <= 1.0)
        {
            // sample the texture, but only if within 0-1 range to avoid clamping
            wfrag = get_pixel(uvpos_var);
        }
    }
    // back of roll
    if (uvpos.x > p + offset + 0.05 && uvpos.x < p + offset + 0.1)
    {
        // trigonometric sine
        float tsin = (uvpos.x - (p + offset + 0.05)) * 20.0;
        // angle from arcsine
        float angle = asin(tsin);
        // compute x sampling coordinate
        if (direction == 0 || direction == 1)
        {
            uv.x = (angle / (M_PI)) * 0.15 + p + offset + 0.05;
        } else
        {
            uv.x = (uvpos.y - 0.5) * pow(cos(angle), 0.02) + 0.5;
        }
        // compute y sampling coordinate
        if (direction == 2 || direction == 3)
        {
            uv.y = (angle / (M_PI)) * 0.15 + p + offset + 0.05;
        } else
        {
            uv.y = (uvpos.y - 0.5) * pow(cos(angle), 0.02) + 0.5;
        }
        vec4 pfrag = vec4(0.0);
        if (uv.x >= 0.0 && uv.x <= 1.0 &&
            uv.y >= 0.0 && uv.y <= 1.0)
        {
            // sample the texture, but only if within 0-1 range to avoid clamping
            if (direction == 1)
            {
                pfrag = get_pixel(vec2(1.0 - uv.x, uv.y));
			} else if (direction == 2)
            {
                pfrag = get_pixel(vec2(uv.x, 1.0 - uv.y));
			} else
            {
                pfrag = get_pixel(uv);
			}
        }
        // store color for fragment mixing with current fragment if translucent
        wfrag = mix(pfrag, wfrag, wfrag.a);
    }
    // front of roll
    if (uvpos.x > p + offset && uvpos.x < p + offset + 0.1)
    {
        // trigonometric sine
        float tsin = (uvpos.x - (p + offset + 0.1)) * 20.0 + 1.0;
        // angle from arcsine
        float angle = asin(tsin);
        // compute x sampling coordinate
        if (direction == 0 || direction == 1)
        {
            uv.x = (angle / (-M_PI)) * 0.1 + p + offset + 0.2;
        } else
        {
            uv.x = (uvpos.y - 0.5) * 0.9 * pow(cos(angle), -0.04) + 0.5;
        }
        // compute y sampling coordinate
        if (direction == 2 || direction == 3)
        {
            uv.y = (angle / (-M_PI)) * 0.1 + p + offset + 0.2;
        } else
        {
            uv.y = (uvpos.y - 0.5) * 0.9 * pow(cos(angle), -0.04) + 0.5;
        }
        vec4 pfrag = vec4(0.0);
        if (uv.x >= 0.0 && uv.x <= 1.0 &&
            uv.y >= 0.0 && uv.y <= 1.0)
        {
            // sample the texture, but only if within 0-1 range to avoid clamping
            if (direction == 1)
            {
                pfrag = get_pixel(vec2(1.0 - uv.x, uv.y));
			} else if (direction == 2)
            {
                pfrag = get_pixel(vec2(uv.x, 1.0 - uv.y));
			} else
            {
                pfrag = get_pixel(uv);
			}
        }
        // compute lighting
        pfrag = vec4(clamp(pfrag.rgb + (angle / -M_PI), 0.0, 1.0), pfrag.a);
        // store color for fragment mixing with current fragment if translucent
        wfrag = mix(wfrag, pfrag, pfrag.a);
    }

    gl_FragColor = wfrag;
}
)";

namespace wf
{
namespace carpet
{
using namespace wf::scene;
using namespace wf::animate;
using namespace wf::animation;

static std::string carpet_transformer_name = "animation-carpet";

wf::option_wrapper_t<int> carpet_direction{"extra-animations/carpet_direction"};

class carpet_transformer : public wf::scene::view_2d_transformer_t
{
  public:
    wayfire_view view;
    wf::output_t *output;
    OpenGL::program_t program;
    wf::auxilliary_buffer_t buffer;
    duration_t progression;

    class simple_node_render_instance_t : public wf::scene::transformer_render_instance_t<transformer_base_node_t>
    {
        wf::signal::connection_t<node_damage_signal> on_node_damaged =
            [=] (node_damage_signal *ev)
        {
            push_to_parent(ev->region);
        };

        carpet_transformer *self;
        wayfire_view view;
        damage_callback push_to_parent;

      public:
        simple_node_render_instance_t(carpet_transformer *self, damage_callback push_damage,
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
                        .damage   = damage & self->get_bounding_box(),
                    });
        }

        void transform_damage_region(wf::region_t& damage) override
        {
            damage |= self->get_bounding_box();
        }

        void render(const wf::scene::render_instruction_t& data) override
        {
            auto bb  = self->get_children_bounding_box();
            auto pbb = self->get_padded_bounding_box();
            auto tex = wf::gles_texture_t{get_texture(1.0)};

            const float vertices[] = {
                -1.0, -1.0,
                1.0f, -1.0,
                1.0f, 1.0f,
                -1.0, 1.0f
            };
            wf::pointf_t offset1{-float(bb.x - pbb.x) / bb.width,
                -float(pbb.height - ((bb.y - pbb.y) + bb.height)) / bb.height};
            wf::pointf_t offset2{float(pbb.width) / bb.width + offset1.x,
                float(pbb.height) / bb.height + offset1.y};
            const float uv[] = {
                float(offset1.x), float(offset2.y),
                float(offset2.x), float(offset2.y),
                float(offset2.x), float(offset1.y),
                float(offset1.x), float(offset1.y),
            };
            auto progress = self->progression.progress();

            data.pass->custom_gles_subpass([&]
            {
                self->buffer.allocate({pbb.width, pbb.height});
                wf::gles::bind_render_buffer(self->buffer.get_renderbuffer());
                wf::gles_texture_t final_tex{self->buffer.get_texture()};
                OpenGL::clear(wf::color_t{0.0, 0.0, 0.0, 0.0}, GL_COLOR_BUFFER_BIT);
                self->program.use(wf::TEXTURE_TYPE_RGBA);
                self->program.attrib_pointer("position", 2, 0, vertices);
                self->program.attrib_pointer("uv_in", 2, 0, uv);
                self->program.uniform1f("progress", progress);
                self->program.uniform1i("direction", carpet_direction);

                self->program.set_active_texture(tex);
                GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

                wf::gles::bind_render_buffer(data.target);
                for (auto box : data.damage)
                {
                    wf::gles::render_target_logic_scissor(data.target, wlr_box_from_pixman_box(box));
                    OpenGL::render_transformed_texture(final_tex, pbb,
                        wf::gles::render_target_orthographic_projection(data.target),
                        glm::vec4(1.0, 1.0, 1.0, 1.0), 0);
                }

                GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
                self->program.deactivate();
                self->buffer.free();
            });
        }
    };

    carpet_transformer(wayfire_view view, wf::geometry_t bbox,
        wf::animation_description_t duration) : wf::scene::view_2d_transformer_t(view)
    {
        this->view = view;
        this->progression = duration_t{wf::create_option(duration)};
        if (view->get_output())
        {
            output = view->get_output();
            output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        }

        wf::gles::run_in_context([&]
        {
            program.compile(carpet_vert_source, carpet_frag_source);
        });
    }

    wf::geometry_t get_padded_bounding_box()
    {
        auto box     = this->get_children_bounding_box();
        auto padding = (box.width > box.height ? box.width : box.height) * 0.07;
        box.x     -= padding;
        box.y     -= padding;
        box.width += padding * 2;
        box.height += padding * 2;
        return box;
    }

    wf::geometry_t get_bounding_box() override
    {
        return get_padded_bounding_box();
    }

    wf::effect_hook_t pre_hook = [=] ()
    {
        output->render->damage(this->get_bounding_box());
    };

    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override
    {
        instances.push_back(std::make_unique<simple_node_render_instance_t>(
            this, push_damage, view));
    }

    void init_animation(bool carpet)
    {
        if (carpet)
        {
            this->progression.reverse();
        }

        this->progression.start();
    }

    virtual ~carpet_transformer()
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

class carpet_animation : public animation_base_t
{
    wayfire_view view;

  public:
    void init(wayfire_view view, wf::animation_description_t dur, animation_type type) override
    {
        this->view = view;
        pop_transformer(view);
        auto bbox = view->get_transformed_node()->get_bounding_box();
        auto tmgr = view->get_transformed_node();
        auto node = std::make_shared<carpet_transformer>(view, bbox, dur);
        tmgr->add_transformer(node, wf::TRANSFORMER_HIGHLEVEL + 1, carpet_transformer_name);
        node->init_animation(type & WF_ANIMATE_HIDING_ANIMATION);
    }

    void pop_transformer(wayfire_view view)
    {
        if (view->get_transformed_node()->get_transformer(carpet_transformer_name))
        {
            view->get_transformed_node()->rem_transformer(carpet_transformer_name);
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
                tmgr->get_transformer<wf::carpet::carpet_transformer>(carpet_transformer_name))
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
                view->get_transformed_node()->get_transformer<wf::carpet::carpet_transformer>(
                    carpet_transformer_name))
        {
            tr->progression.reverse();
        }
    }
};
}
}
