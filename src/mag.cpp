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

#include "wayfire/txn/transaction-manager.hpp"
#include "wayfire/core.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/plugin.hpp"
#include <wayfire/workspace-set.hpp>
#include <wayfire/geometry.hpp>
#include <wayfire/scene-operations.hpp>
#include "wayfire/output.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/output-layout.hpp"
#include "wayfire/render-manager.hpp"
#include "wayfire/opengl.hpp"
#include "wayfire/per-output-plugin.hpp"
#include <wayfire/scene.hpp>
#include <wayfire/signal-provider.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/scene-render.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <wayfire/nonstd/tracking-allocator.hpp>
#include <wayfire/txn/transaction-object.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/view.hpp>

namespace wf
{
namespace scene
{
class mag_view_t : public wf::toplevel_view_interface_t
{
    class mag_node_t : public floating_inner_node_t
    {
        class color_rect_render_instance_t : public simple_render_instance_t<mag_node_t>
        {
          public:
            using simple_render_instance_t::simple_render_instance_t;
            void render(const wf::scene::render_instruction_t& data) override
            {
                auto view = self->_view.lock();
                if (!view)
                {
                    return;
                }

                auto geometry = self->get_bounding_box();
                /* Draw the inside of the rect */
                data.pass->add_texture({view->mag_tex.get_texture()}, data.target, geometry, data.damage);
            }
        };

        std::weak_ptr<mag_view_t> _view;

      public:
        mag_node_t(std::weak_ptr<mag_view_t> view) : floating_inner_node_t(false)
        {
            _view = view;
        }

        void gen_render_instances(std::vector<scene::render_instance_uptr>& instances,
            scene::damage_callback push_damage, wf::output_t *output) override
        {
            instances.push_back(std::make_unique<color_rect_render_instance_t>(this, push_damage, output));
        }

        wf::geometry_t get_bounding_box() override
        {
            auto view = _view.lock();
            if (view)
            {
                return view->get_geometry();
            } else
            {
                return {0, 0, 0, 0};
            }
        }

        std::optional<wf::scene::input_node_t> find_node_at(const wf::pointf_t& at) override
        {
            auto view = _view.lock();
            if (!view)
            {
                return {};
            }

            if ((view->get_geometry() & at))
            {
                return wf::scene::input_node_t{
                    .node = this,
                    .local_coords = at,
                };
            }

            return {};
        }
    };

    /**
     * Implementation of the toplevel interface for the mag view.
     * It is rather simple, because we do not have to wait for a client to reconfigure itself.
     */
    class mag_toplevel_t : public wf::toplevel_t
    {
        std::weak_ptr<mag_view_t> _view;

      public:
        mag_toplevel_t(std::weak_ptr<mag_view_t> _view)
        {
            this->_view = _view;
        }

        void commit() override
        {
            _committed = _pending;
            txn::emit_object_ready(this);
        }

        void apply() override
        {
            auto old_state = _current;
            _current = _committed;
            if (auto view = _view.lock())
            {
                if (!old_state.mapped && _current.mapped)
                {
                    view->map();
                }

                if (old_state.mapped && !_current.mapped)
                {
                    view->unmap(true);
                }

                wf::view_implementation::emit_toplevel_state_change_signals(view, old_state);
            }
        }
    };

    bool _is_mapped = false;

  public:
    wf::auxilliary_buffer_t mag_tex;

    mag_view_t() : wf::toplevel_view_interface_t()
    {
        this->role = wf::VIEW_ROLE_TOPLEVEL;
    }

    static std::shared_ptr<mag_view_t> create(wf::output_t *output)
    {
        auto self = wf::view_interface_t::create<mag_view_t>();

        auto toplevel = std::make_shared<mag_toplevel_t>(self);
        self->set_toplevel(toplevel);
        auto surface_node = std::make_shared<mag_node_t>(self);
        self->set_surface_root_node(surface_node);

        self->set_output(output);
        return self;
    }

    wlr_surface *get_keyboard_focus_surface() override
    {
        return nullptr;
    }

    void map()
    {
        _is_mapped = true;
        wf::scene::set_node_enabled(get_root_node(), true);

        if (get_output())
        {
            wf::scene::readd_front(get_output()->wset()->get_node(), get_root_node());
            get_output()->wset()->add_view({this});
        }

        emit_view_map();
    }

    void unmap(bool animate)
    {
        if (animate)
        {
            emit_view_pre_unmap();
        }

        _is_mapped = false;
        wf::scene::set_node_enabled(get_root_node(), false);
        emit_view_unmap();
    }

    void close() override
    {
        toplevel()->pending().mapped = false;
        wf::get_core().tx_manager->schedule_object(toplevel());
    }

    bool is_mapped() const override
    {
        return _is_mapped;
    }
};

class wayfire_magnifier : public wf::per_output_plugin_instance_t
{
    const std::string transformer_name = "mag";
    wf::option_wrapper_t<wf::activatorbinding_t> toggle_binding{"mag/toggle"};
    wf::option_wrapper_t<int> zoom_level{"mag/zoom_level"};
    std::shared_ptr<mag_view_t> mag_view;
    bool active = false, hook_set = false;
    int width, height;
    wf::plugin_activation_data_t grab_interface{
        .name = transformer_name,
        .capabilities = 0,
    };

    wf::activator_callback toggle_cb = [=] (auto)
    {
        active = !active;
        if (active || (mag_view && mag_view->minimized))
        {
            return activate();
        } else
        {
            deactivate();
            return true;
        }
    };

    wf::signal::connection_t<view_unmapped_signal> on_mag_unmap = [=] (auto)
    {
        active = false;
        deactivate();
    };

    wf::option_wrapper_t<int> default_height{"mag/default_height"};

  public:
    void init() override
    {
        if (!wf::get_core().is_gles2())
        {
            LOGE("mag plugin requires GLES2 renderer!");
            return;
        }

        output->add_activator(toggle_binding, &toggle_cb);
        hook_set = active = false;
    }

    void ensure_preview()
    {
        if (mag_view)
        {
            if (mag_view->minimized)
            {
                mag_view->set_minimized(false);
            }

            return;
        }

        mag_view = mag_view_t::create(output);
        mag_view->connect(&on_mag_unmap);
    }

    wf::geometry_t get_default_geometry()
    {
        auto og = output->get_relative_geometry();
        float aspect = (float)og.width / og.height;
        wf::geometry_t start_geometry = {100, 100, (int)(default_height * aspect), default_height};
        return start_geometry;
    }

    bool activate()
    {
        if (mag_view && mag_view->minimized && hook_set)
        {
            mag_view->set_minimized(false);
            return true;
        }

        if (!output->activate_plugin(&grab_interface))
        {
            return false;
        }

        if (!hook_set)
        {
            output->render->add_effect(&post_hook, wf::OUTPUT_EFFECT_POST);
            wlr_output_lock_software_cursors(output->handle, true);
            hook_set = true;
        }

        ensure_preview();

        mag_view->toplevel()->pending().mapped   = true;
        mag_view->toplevel()->pending().geometry = get_default_geometry();
        wf::get_core().tx_manager->schedule_object(mag_view->toplevel());
        return true;
    }

    wf::effect_hook_t post_hook = [=] ()
    {
        auto cursor_position = output->get_cursor_position();
        auto ortho =
            wf::gles::render_target_orthographic_projection(output->render->get_target_framebuffer());

        // Map from OpenGL coordinates to [0, 1]x[0, 1]
        auto cursor_transform =
            glm::translate(glm::mat4(1.0), glm::vec3(0.5, 0.5, 0.0)) *
            glm::scale(glm::mat4(1.0), glm::vec3{0.5, -0.5, 1.0}) * ortho;

        glm::vec4 cursor = glm::vec4(cursor_position.x, cursor_position.y, 0.0, 1.0);
        cursor = cursor_transform * cursor;

        float x = cursor.x;
        float y = cursor.y;

        auto og = output->get_relative_geometry();

        width  = og.width;
        height = og.height;

        /* min and max represent the distance on either side of the pointer.
         * The min is 0.5 and means no zoom, half the screen on either side
         * of the pointer. max is 0.01 and means this much of the screen
         * on either side, which is about the maximum reasonable zoom level. */
        float min   = 0.5;
        float max   = 0.01;
        float range = min - max;
        float level = (1.0 - (zoom_level / 100.0)) * range + max;

        /* Compute zoom_box, forcing the zoom to stay on the output */
        gl_geometry zoom_box;

        /* Y-invert */
        y = 1.0 - y;

        zoom_box.x1 = x - level;
        zoom_box.y1 = y - level;
        zoom_box.x2 = x + level;
        zoom_box.y2 = y + level;

        if (zoom_box.x1 < 0.0)
        {
            zoom_box.x2 -= zoom_box.x1;
            zoom_box.x1  = 0.0;
        }

        if (zoom_box.y1 < 0.0)
        {
            zoom_box.y2 -= zoom_box.y1;
            zoom_box.y1  = 0.0;
        }

        if (zoom_box.x2 > 1.0)
        {
            zoom_box.x1 += 1.0 - zoom_box.x2;
            zoom_box.x2  = 1.0;
        }

        if (zoom_box.y2 > 1.0)
        {
            zoom_box.y1 += 1.0 - zoom_box.y2;
            zoom_box.y2  = 1.0;
        }

        zoom_box.x1 *= width - 1;
        zoom_box.x2 *= width - 1;
        zoom_box.y1 *= height - 1;
        zoom_box.y2 *= height - 1;

        /* Copy zoom_box part of the output to our own texture to be
         * read by the mag_view_t. */
        if (mag_view->mag_tex.allocate(wf::dimensions(og)) == wf::buffer_reallocation_result_t::REALLOCATED)
        {
            // Clear the buffer if reallocated
            wf::gles::run_in_context([&]
            {
                wf::gles::bind_render_buffer(mag_view->mag_tex.get_renderbuffer());
                OpenGL::clear({0, 0, 0, 0});
            });
        }

        wf::gles::run_in_context([&]
        {
            auto src_fb_id = wf::gles::ensure_render_buffer_fb_id(output->render->get_target_framebuffer());
            wf::gles::bind_render_buffer(mag_view->mag_tex.get_renderbuffer());
            GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, src_fb_id));
            GL_CALL(glBlitFramebuffer(zoom_box.x1, zoom_box.y2, zoom_box.x2, zoom_box.y1,
                0, 0, width, height,
                GL_COLOR_BUFFER_BIT, GL_LINEAR));
        });

        mag_view->damage();
    };

    void deactivate()
    {
        output->deactivate_plugin(&grab_interface);

        if (hook_set)
        {
            output->render->rem_effect(&post_hook);
            wlr_output_lock_software_cursors(output->handle, false);
            hook_set = false;
        }

        output->render->damage_whole();

        if (!mag_view || !mag_view->is_mapped())
        {
            active = false;
            return;
        }

        mag_view->close();
    }

    void fini() override
    {
        if (mag_view)
        {
            mag_view->unmap(false);
        }

        deactivate();
        output->rem_binding(&toggle_cb);
    }
};
}
}

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wf::scene::wayfire_magnifier>);
