/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Scott Moreau
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

#include "wayfire/core.hpp"
#include "wayfire/view.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/output.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/workspace-manager.hpp"
#include "wayfire/output-layout.hpp"
#include "wayfire/compositor-surface.hpp"
#include "wayfire/compositor-view.hpp"
#include "wayfire/render-manager.hpp"
#include "wayfire/opengl.hpp"
#include <glm/gtc/matrix_transform.hpp>

#include <wayfire/util/log.hpp>


class mag_view_t : public wf::color_rect_view_t
{
    wf::option_wrapper_t<int> default_height{"mag/default_height"};

  public:
    wf::framebuffer_t mag_tex;

    mag_view_t(wf::output_t *output, float aspect) :
        wf::color_rect_view_t()
    {
        set_output(output);

        set_geometry({100, 100, (int)(default_height * aspect), default_height});

        this->role = wf::VIEW_ROLE_TOPLEVEL;
        output->workspace->add_view(self(), wf::LAYER_TOP);
    }

    bool accepts_input(int32_t sx, int32_t sy) override
    {
        auto vg = get_wm_geometry();

        /* Allow move and resize */
        if ((0 < sx) && (sx < vg.width) && (0 < sy) && (sy < vg.height))
        {
            return true;
        }

        return false;
    }

    void simple_render(const wf::framebuffer_t& fb, int x, int y,
        const wf::region_t& damage) override
    {
        OpenGL::render_begin(fb);
        auto vg = get_wm_geometry();
        gl_geometry src_geometry = {(float)vg.x, (float)vg.y,
            (float)vg.x + vg.width, (float)vg.y + vg.height};
        for (const auto& box : damage)
        {
            fb.logic_scissor(wlr_box_from_pixman_box(box));
            OpenGL::render_transformed_texture(mag_tex.tex, src_geometry, {},
                fb.get_orthographic_projection(),
                glm::vec4(1.0), 0);
        }

        OpenGL::render_end();
    }

    virtual ~mag_view_t()
    {}
};

class wayfire_magnifier : public wf::plugin_interface_t
{
    const std::string transformer_name = "mag";
    wf::option_wrapper_t<wf::activatorbinding_t> toggle_binding{"mag/toggle"};
    wf::option_wrapper_t<int> zoom_level{"mag/zoom_level"};
    nonstd::observer_ptr<mag_view_t> mag_view;
    bool active, hook_set;
    int width, height;

    wf::activator_callback toggle_cb = [=] (auto)
    {
        active = !active;
        if (active)
        {
            return activate();
        } else
        {
            deactivate();

            return true;
        }
    };

  public:
    void init() override
    {
        grab_interface->name = transformer_name;
        grab_interface->capabilities = 0;

        output->add_activator(toggle_binding, &toggle_cb);
        hook_set = active = false;
    }

    void ensure_preview()
    {
        if (mag_view)
        {
            return;
        }

        auto og   = output->get_relative_geometry();
        auto view =
            std::make_unique<mag_view_t>(output, (float)og.width / og.height);

        mag_view = {view};

        wf::get_core().add_view(std::move(view));
    }

    bool activate()
    {
        if (!output->activate_plugin(grab_interface))
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

        return true;
    }

    wf::effect_hook_t post_hook = [=] ()
    {
        auto cursor_position = output->get_cursor_position();

        auto ortho = output->render->get_target_framebuffer()
            .get_orthographic_projection();

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

        OpenGL::render_begin();
        mag_view->mag_tex.allocate(width, height);
        mag_view->mag_tex.geometry = og;
        mag_view->mag_tex.bind();
        GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER,
            output->render->get_target_framebuffer().fb));
        GL_CALL(glBlitFramebuffer(zoom_box.x1, zoom_box.y2, zoom_box.x2, zoom_box.y1,
            0, 0, width, height,
            GL_COLOR_BUFFER_BIT, GL_LINEAR));
        OpenGL::render_end();

        mag_view->damage();
    };

    void deactivate()
    {
        output->deactivate_plugin(grab_interface);

        if (hook_set)
        {
            output->render->rem_effect(&post_hook);
            wlr_output_lock_software_cursors(output->handle, false);
            hook_set = false;
        }

        output->render->damage_whole();

        if (!mag_view)
        {
            return;
        }

        mag_view->close();
        mag_view = nullptr;
    }

    void fini() override
    {
        deactivate();
        output->rem_binding(&toggle_cb);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_magnifier);
