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

    void set_active_status(bool status)
    {
        if (this->active == status)
        {
            return;
        }

        if (status)
        {
            output->render->add_effect(&overlay_hook, wf::OUTPUT_EFFECT_OVERLAY);
            output->render->add_effect(&on_main_pass_done, wf::OUTPUT_EFFECT_PASS_DONE);
        } else
        {
            output->render->rem_effect(&overlay_hook);
            output->render->rem_effect(&on_main_pass_done);
        }

        this->active = status;
    }

    wf::activator_callback toggle_cb = [=] (auto)
    {
        set_active_status(!active);
        output->render->damage_whole();
        return true;
    };

    bool egl_extension_supported(std::string ext)
    {
        if (!wf::get_core().is_gles2())
        {
            return false;
        }

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
        color.r = 0.15 + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / 0.25));
        color.g = 0.15 + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / 0.25));
        color.b = 0.15 + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / 0.25));
        color.a = 0.25;
    }

    wf::effect_hook_t overlay_hook = [=] ()
    {
        auto target_fb = output->render->get_target_framebuffer();
        wf::region_t swap_damage = target_fb.geometry_region_from_framebuffer_region(
            output->render->get_swap_damage());

        wf::region_t scheduled_damage = output->render->get_scheduled_damage();
        wf::region_t output_region{target_fb.geometry};
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
        inverted_damage = output_region ^ damage;

        auto rpass = output->render->get_current_pass();
        rpass->add_rect(color, target_fb, target_fb.geometry, damage);
        if (reduce_flicker)
        {
            /* Show swap damage. It might be possible that we blit right over this but in the case of cube
             * and expo, it shows client and swap damage in contrast. This makes sense since the idea is to
             * show damage as colored regions. We don't do this if the reduce_flicker option isn't set because
             * we don't repaint the inverted damage from the last buffer in this case, so we would keep
             * painting it with different colors until it is white. */
            get_random_color(color);
            rpass->add_rect(color, target_fb, target_fb.geometry, inverted_damage);
        }

        /* If swap_buffers_with_damage is supported, we do not need the following to be executed. */
        if (egl_swap_buffers_with_damage || !reduce_flicker)
        {
            return;
        }

        /* Repaint the inverted damage region with the last buffer contents.
         * We only want to see what actually changed on screen. If we don't do this, things like mouse and
         * keyboard input cause buffer swaps which only make the screen flicker between buffers, without
         * showing any actual damage changes. If swap_buffers_with_damage is supported, we do not need to do
         * this since the damage region that is passed to swap is only repainted. If it isn't supported, the
         * entire buffer is repainted.
         */
        if (last_buffer.get_size().width > 0)
        {
            wf::texture_t texture;
            texture.texture   = last_buffer.get_texture();
            texture.transform = target_fb.wl_transform;
            rpass->add_texture(texture, target_fb, target_fb.geometry, inverted_damage);
        }
    };

    wf::effect_hook_t on_main_pass_done = [=] ()
    {
        if (!reduce_flicker || egl_swap_buffers_with_damage)
        {
            return;
        }

        /*
         * Save the current buffer to last buffer so we can render the
         * inverted damage from the last buffer to the current buffer
         * on next frame. We have to save the entire buffer because we
         * don't know what the next frame damage will be.
         */
        auto target_fb = output->render->get_target_framebuffer();
        last_buffer.allocate(target_fb.get_size());
        wlr_box full = wf::construct_box({0, 0}, target_fb.get_size());
        last_buffer.get_renderbuffer().blit(target_fb, wf::geometry_to_fbox(full), full);
    };

    void fini() override
    {
        output->rem_binding(&toggle_cb);
        set_active_status(false);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_showrepaint>);
