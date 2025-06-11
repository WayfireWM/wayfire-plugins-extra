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

#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/per-output-plugin.hpp>

#include <wayfire/util/log.hpp>

extern "C"
{
#include <EGL/egl.h>
}

class wayfire_showrepaint : public wf::per_output_plugin_instance_t
{
    wf::option_wrapper_t<wf::activatorbinding_t> toggle_binding{"showrepaint/toggle"};
    wf::option_wrapper_t<bool> reduce_flicker{"showrepaint/reduce_flicker"};
    bool active, egl_swap_buffers_with_damage;
    wf::auxilliary_buffer_t last_buffer;

  public:
    void init() override
    {
        if (!wf::get_core().is_gles2())
        {
            LOGE("showrepaint plugin requires GLES2 renderer!");
            return;
        }

        active = false;
        egl_swap_buffers_with_damage =
            egl_extension_supported("EGL_KHR_swap_buffers_with_damage") ||
            egl_extension_supported("EGL_EXT_swap_buffers_with_damage");
        output->add_activator(toggle_binding, &toggle_cb);
        reduce_flicker.set_callback(option_changed);
    }

    wf::config::option_base_t::updated_callback_t option_changed = [=] ()
    {
        output->render->damage_whole();
    };

    wf::activator_callback toggle_cb = [=] (auto)
    {
        active = !active;

        if (active)
        {
            output->render->add_effect(&overlay_hook, wf::OUTPUT_EFFECT_OVERLAY);
        } else
        {
            output->render->rem_effect(&overlay_hook);
        }

        output->render->damage_whole();

        return true;
    };

    bool egl_extension_supported(std::string ext)
    {
        std::string extensions;
        wf::gles::run_in_context([&]
        {
            EGLDisplay egl_display = eglGetCurrentDisplay();
            extensions = std::string(eglQueryString(egl_display, EGL_EXTENSIONS));
        });

        size_t pos = extensions.find(ext);
        if (pos == std::string::npos)
        {
            return false;
        }

        return true;
    }

    void get_random_color(wf::color_t& color)
    {
        color.r = 0.15 + static_cast<float>(rand()) /
            (static_cast<float>(RAND_MAX / 0.25));
        color.g = 0.15 + static_cast<float>(rand()) /
            (static_cast<float>(RAND_MAX / 0.25));
        color.b = 0.15 + static_cast<float>(rand()) /
            (static_cast<float>(RAND_MAX / 0.25));
        color.a = 0.25;
    }

    wf::effect_hook_t overlay_hook = [=] ()
    {
        auto target_fb = output->render->get_target_framebuffer();
        wf::region_t swap_damage = output->render->get_swap_damage();
        wf::region_t scheduled_damage = output->render->get_scheduled_damage();
        wlr_box fbg = {0, 0, target_fb.get_size().width, target_fb.get_size().height};
        wf::region_t output_region{fbg};
        wf::region_t inverted_damage;
        wf::region_t damage;

        /* Show scheduled client damage. Scheduled damage is the client damage
         * in union with last frame client damage. If this region is empty, we
         * use swap damage, which is the same as scheduled damage unless something
         * is rendering the entire frame buffer, in which case it is the whole
         * output region. The reason for this is because we want to display both
         * scheduled client damage region and the swap damage region, in contrast.
         */
        wf::color_t color;
        get_random_color(color);
        damage = scheduled_damage.empty() ? swap_damage : scheduled_damage;

        last_buffer.allocate({fbg.width, fbg.height});
        GLuint last_buffer_fb_id = wf::gles::ensure_render_buffer_fb_id(last_buffer.get_renderbuffer());
        GLuint target_fb_fb_id   = wf::gles::ensure_render_buffer_fb_id(target_fb);

        output->render->get_current_pass()->custom_gles_subpass([&]
        {
            wf::gles::bind_render_buffer(target_fb);
            for (const auto& b : damage)
            {
                wlr_box box{b.x1, b.y1, b.x2 - b.x1, b.y2 - b.y1};
                OpenGL::render_rectangle(box, color,
                    wf::gles::render_target_orthographic_projection(target_fb));
            }

            if (reduce_flicker)
            {
                /* Show swap damage. It might be possible that we blit right over this
                 * but in the case of cube and expo, it shows client and swap damage in
                 * contrast. This makes sense since the idea is to show damage as colored
                 * regions. We don't do this if the reduce_flicker option isn't set
                 * because
                 * we don't repaint the inverted damage from the last buffer in this
                 * case,
                 * so we would keep painting it with different colors until it is white.
                 * */
                get_random_color(color);
                inverted_damage = output_region ^ damage;
                for (const auto& b : inverted_damage)
                {
                    wlr_box box{b.x1, b.y1, b.x2 - b.x1, b.y2 - b.y1};
                    OpenGL::render_rectangle(box, color,
                        wf::gles::render_target_orthographic_projection(target_fb));
                }
            }

            /* If swap_buffers_with_damage is supported, we do not need the
             * following to be executed. */
            if (egl_swap_buffers_with_damage)
            {
                return;
            }

            /* User option. */
            if (!reduce_flicker)
            {
                return;
            }

            /* Repaint the inverted damage region with the last buffer contents.
             * We only want to see what actually changed on screen. If we don't
             * do this, things like mouse and keyboard input cause buffer swaps
             * which only make the screen flicker between buffers, without showing
             * any actual damage changes. If swap_buffers_with_damage is supported,
             * we do not need to do this since the damage region that is passed to
             * swap is only repainted. If it isn't supported, the entire buffer is
             * repainted.
             */
            GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, last_buffer_fb_id));
            GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target_fb_fb_id));
            damage = swap_damage.empty() ? scheduled_damage : swap_damage;
            output_region   *= target_fb.scale;
            inverted_damage  = output_region ^ damage;
            inverted_damage *= 1.0 / target_fb.scale;
            for (const auto& rect : inverted_damage)
            {
                pixman_box32_t b = pixman_box_from_wlr_box(
                    target_fb.framebuffer_box_from_geometry_box(
                        wlr_box_from_pixman_box(rect)));
                GL_CALL(glBlitFramebuffer(
                    b.x1, fbg.height - b.y2,
                    b.x2, fbg.height - b.y1,
                    b.x1, fbg.height - b.y2,
                    b.x2, fbg.height - b.y1,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR));
            }

            /* Save the current buffer to last buffer so we can render the
             * inverted damage from the last buffer to the current buffer
             * on next frame. We have to save the entire buffer because we
             * don't know what the next frame damage will be.
             */
            GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, target_fb_fb_id));
            GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, last_buffer_fb_id));
            GL_CALL(glBlitFramebuffer(
                0, 0, fbg.width, fbg.height,
                0, 0, fbg.width, fbg.height,
                GL_COLOR_BUFFER_BIT, GL_LINEAR));
        });
    };

    void fini() override
    {
        output->rem_binding(&toggle_cb);
        output->render->rem_effect(&overlay_hook);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_showrepaint>);
