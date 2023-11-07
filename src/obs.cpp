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
 */

#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>


static const char *vertex_shader =
    R"(
#version 100

attribute mediump vec2 position;
attribute mediump vec2 texcoord;

varying mediump vec2 uvpos;

uniform mat4 mvp;

void main() {

   gl_Position = mvp * vec4(position.xy, 0.0, 1.0);
   uvpos = texcoord;
}
)";

static const char *fragment_shader =
    R"(
#version 100
@builtin_ext@
@builtin@

precision mediump float;

/* Input uniforms are 0-1 range. */
uniform mediump float opacity;
uniform mediump float brightness;
uniform mediump float saturation;

varying mediump vec2 uvpos;

vec3 saturate(vec3 rgb, float adjustment)
{
    // Algorithm from Chapter 16 of OpenGL Shading Language
    const vec3 w = vec3(0.2125, 0.7154, 0.0721);
    vec3 intensity = vec3(dot(rgb, w));
    return mix(intensity, rgb, adjustment);
}

void main()
{
    vec4 c = get_pixel(uvpos);
    /* opacity */
    c = c * opacity;
    /* brightness */
    c = vec4(c.rgb * brightness, c.a);
    /* saturation */
    c = vec4(saturate(c.rgb, saturation), c.a);
    gl_FragColor = c;
}
)";

namespace wf
{
namespace scene
{
namespace obs
{
const std::string transformer_name = "obs";

class wf_obs : public wf::scene::view_2d_transformer_t
{
    wayfire_view view;
    OpenGL::program_t *program;
    std::unique_ptr<wf::animation::simple_animation_t> opacity;
    std::unique_ptr<wf::animation::simple_animation_t> brightness;
    std::unique_ptr<wf::animation::simple_animation_t> saturation;

  public:
    class simple_node_render_instance_t : public wf::scene::transformer_render_instance_t<node_t>
    {
        wf::signal::connection_t<node_damage_signal> on_node_damaged =
            [=] (node_damage_signal *ev)
        {
            push_to_parent(ev->region);
        };

        wf_obs *self;
        wayfire_view view;
        wf::output_t *wo = nullptr;
        wf::effect_hook_t pre_hook;
        damage_callback push_to_parent;

      public:
        simple_node_render_instance_t(wf_obs *self, damage_callback push_damage,
            wayfire_view view) : wf::scene::transformer_render_instance_t<node_t>(self,
                push_damage,
                view->get_output())
        {
            this->self = self;
            this->view = view;
            this->push_to_parent = push_damage;
            self->connect(&on_node_damaged);

            if (view->get_output())
            {
                wo = view->get_output();
                pre_hook = [=] ()
                {
                    if (this->self->progression_running())
                    {
                        this->view->damage();
                    } else
                    {
                        wo->render->rem_effect(&pre_hook);
                        if (this->self->transformer_inert() &&
                            view->get_transformed_node()->get_transformer(transformer_name))
                        {
                            self->disconnect(&on_node_damaged);
                            view->get_transformed_node()->rem_transformer(transformer_name);
                        }
                    }
                };
            }
        }

        ~simple_node_render_instance_t()
        {
            if (wo)
            {
                wo->render->rem_effect(&pre_hook);
            }
        }

        void schedule_instructions(
            std::vector<render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::region_t& damage)
        {
            // We want to render ourselves only, the node does not have children
            instructions.push_back(render_instruction_t{
                            .instance = this,
                            .target   = target,
                            .damage   = damage & self->get_bounding_box(),
                        });
            if (wo && this->self->progression_running())
            {
                wo->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
            }
        }

        void render(const wf::render_target_t& target,
            const wf::region_t& region)
        {
            wlr_box fb_geom =
                target.framebuffer_box_from_geometry_box(target.geometry);
            auto view_box = target.framebuffer_box_from_geometry_box(
                self->get_children_bounding_box());
            view_box.x -= fb_geom.x;
            view_box.y -= fb_geom.y;

            float x = view_box.x, y = view_box.y, w = view_box.width,
                h = view_box.height;

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

            OpenGL::render_begin(target);

            /* Upload data to shader */
            auto src_tex = wf::scene::transformer_render_instance_t<node_t>::get_texture(
                1.0);
            this->self->program->use(src_tex.type);
            this->self->program->uniform1f("opacity", this->self->get_opacity());
            this->self->program->uniform1f("brightness", this->self->get_brightness());
            this->self->program->uniform1f("saturation", this->self->get_saturation());
            this->self->program->attrib_pointer("position", 2, 0, vertexData);
            this->self->program->attrib_pointer("texcoord", 2, 0, texCoords);
            this->self->program->uniformMatrix4f("mvp", target.transform);
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            this->self->program->set_active_texture(src_tex);

            /* Render it to target */
            target.bind();
            GL_CALL(glViewport(x, fb_geom.height - y - h, w, h));

            GL_CALL(glEnable(GL_BLEND));
            GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

            for (const auto& box : region)
            {
                target.logic_scissor(wlr_box_from_pixman_box(box));
                GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
            }

            /* Disable stuff */
            GL_CALL(glDisable(GL_BLEND));
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));

            this->self->program->deactivate();
            OpenGL::render_end();
        }
    };

    wf_obs(wayfire_view view, OpenGL::program_t *program) : wf::scene::view_2d_transformer_t(view)
    {
        this->view    = view;
        this->program = program;

        opacity    = std::make_unique<wf::animation::simple_animation_t>(wf::create_option<int>(500));
        brightness = std::make_unique<wf::animation::simple_animation_t>(wf::create_option<int>(500));
        saturation = std::make_unique<wf::animation::simple_animation_t>(wf::create_option<int>(500));

        opacity->set(1.0, 1.0);
        brightness->set(1.0, 1.0);
        saturation->set(1.0, 1.0);
    }

    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override
    {
        instances.push_back(std::make_unique<simple_node_render_instance_t>(
            this, push_damage, view));
    }

    void set_opacity_duration(int duration)
    {
        double o = *opacity;
        opacity.reset();
        opacity = std::make_unique<wf::animation::simple_animation_t>(wf::create_option<int>(duration));
        opacity->set(o, o);
    }

    void set_brightness_duration(int duration)
    {
        double b = *brightness;
        brightness.reset();
        brightness = std::make_unique<wf::animation::simple_animation_t>(wf::create_option<int>(duration));
        brightness->set(b, b);
    }

    void set_saturation_duration(int duration)
    {
        double s = *saturation;
        saturation.reset();
        saturation = std::make_unique<wf::animation::simple_animation_t>(wf::create_option<int>(duration));
        saturation->set(s, s);
    }

    bool transformer_inert()
    {
        return *opacity > 0.99 && *brightness > 0.99 &&
               *saturation > 0.99;
    }

    bool progression_running()
    {
        return opacity->running() || brightness->running() ||
               saturation->running();
    }

    float get_opacity()
    {
        return *opacity;
    }

    float get_brightness()
    {
        return *brightness;
    }

    float get_saturation()
    {
        return *saturation;
    }

    void set_opacity(float target, int duration)
    {
        set_opacity_duration(duration);
        opacity->animate(target);
        this->view->damage();
    }

    void set_brightness(float target, int duration)
    {
        set_brightness_duration(duration);
        brightness->animate(target);
        this->view->damage();
    }

    void set_saturation(float target, int duration)
    {
        set_saturation_duration(duration);
        saturation->animate(target);
        this->view->damage();
    }

    virtual ~wf_obs()
    {
        opacity.reset();
        brightness.reset();
        saturation.reset();
    }
};

class wayfire_obs : public wf::plugin_interface_t
{
    OpenGL::program_t program;
    std::map<wayfire_view, std::shared_ptr<wf_obs>> transformers;
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc_repo;

    void pop_transformer(wayfire_view view)
    {
        if (view->get_transformed_node()->get_transformer(transformer_name))
        {
            view->get_transformed_node()->rem_transformer(transformer_name);
        }
    }

    void remove_transformers()
    {
        for (auto& view : wf::get_core().get_all_views())
        {
            pop_transformer(view);
        }
    }

  public:
    void init() override
    {
        ipc_repo->register_method("wf/obs/set-view-opacity", ipc_set_view_opacity);
        ipc_repo->register_method("wf/obs/set-view-brightness", ipc_set_view_brightness);
        ipc_repo->register_method("wf/obs/set-view-saturation", ipc_set_view_saturation);

        OpenGL::render_begin();
        program.compile(vertex_shader, fragment_shader);
        OpenGL::render_end();
    }

    std::shared_ptr<wf_obs> ensure_transformer(wayfire_view view)
    {
        auto tmgr = view->get_transformed_node();
        if (!tmgr->get_transformer<wf::scene::node_t>(transformer_name))
        {
            auto node = std::make_shared<wf_obs>(view, &program);
            tmgr->add_transformer(node, wf::TRANSFORMER_2D, transformer_name);
        }

        return tmgr->get_transformer<wf_obs>(transformer_name);
    }

    void adjust_opacity(wayfire_view view, float opacity, int duration)
    {
        transformers[view]->set_opacity(opacity, duration);
    }

    void adjust_brightness(wayfire_view view, float brightness, int duration)
    {
        transformers[view]->set_brightness(brightness, duration);
    }

    void adjust_saturation(wayfire_view view, float saturation, int duration)
    {
        transformers[view]->set_saturation(saturation, duration);
    }

    wf::ipc::method_callback ipc_set_view_opacity = [=] (nlohmann::json data) -> nlohmann::json
    {
        WFJSON_EXPECT_FIELD(data, "view-id", number_unsigned);
        WFJSON_EXPECT_FIELD(data, "opacity", number);
        WFJSON_EXPECT_FIELD(data, "duration", number);

        auto view = wf::ipc::find_view_by_id(data["view-id"]);
        if (view && view->is_mapped())
        {
            transformers[view] = ensure_transformer(view);
            adjust_opacity(view, data["opacity"], data["duration"]);
        } else
        {
            return wf::ipc::json_error("Failed to find view with given id. Maybe it was closed?");
        }

        return wf::ipc::json_ok();
    };

    wf::ipc::method_callback ipc_set_view_brightness = [=] (nlohmann::json data) -> nlohmann::json
    {
        WFJSON_EXPECT_FIELD(data, "view-id", number_unsigned);
        WFJSON_EXPECT_FIELD(data, "brightness", number);
        WFJSON_EXPECT_FIELD(data, "duration", number);

        auto view = wf::ipc::find_view_by_id(data["view-id"]);
        if (view && view->is_mapped())
        {
            transformers[view] = ensure_transformer(view);
            adjust_brightness(view, data["brightness"], data["duration"]);
        } else
        {
            return wf::ipc::json_error("Failed to find view with given id. Maybe it was closed?");
        }

        return wf::ipc::json_ok();
    };

    wf::ipc::method_callback ipc_set_view_saturation = [=] (nlohmann::json data) -> nlohmann::json
    {
        WFJSON_EXPECT_FIELD(data, "view-id", number_unsigned);
        WFJSON_EXPECT_FIELD(data, "saturation", number);
        WFJSON_EXPECT_FIELD(data, "duration", number);

        auto view = wf::ipc::find_view_by_id(data["view-id"]);
        if (view && view->is_mapped())
        {
            transformers[view] = ensure_transformer(view);
            adjust_saturation(view, data["saturation"], data["duration"]);
        } else
        {
            return wf::ipc::json_error("Failed to find view with given id. Maybe it was closed?");
        }

        return wf::ipc::json_ok();
    };

    void fini() override
    {
        ipc_repo->unregister_method("wf/obs/set-view-opacity");
        ipc_repo->unregister_method("wf/obs/set-view-brightness");
        ipc_repo->unregister_method("wf/obs/set-view-saturation");

        remove_transformers();

        OpenGL::render_begin();
        program.free_resources();
        OpenGL::render_end();
    }
};
}
}
}

DECLARE_WAYFIRE_PLUGIN(wf::scene::obs::wayfire_obs);
