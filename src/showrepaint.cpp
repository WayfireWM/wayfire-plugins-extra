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

#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/render-manager.hpp>

#include <wayfire/util/log.hpp>

class wayfire_showrepaint : public wf::plugin_interface_t
{
    wf::option_wrapper_t<wf::activatorbinding_t> toggle_binding{"showrepaint/toggle"};

    bool active, hook_set;
    wf::region_t damage;
    wf::framebuffer_base_t last_buffer;

    public:
    void init() override
    {
        active = false;

        output->add_activator(toggle_binding, &toggle_cb);
    }

    void toggle()
    {
        active = !active;

        if (active)
        {
            output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
            output->render->add_effect(&overlay_hook, wf::OUTPUT_EFFECT_OVERLAY);
        }
        else
        {
            output->render->rem_effect(&pre_hook);
            output->render->rem_effect(&overlay_hook);
        }

        output->render->damage_whole();
    }

    wf::activator_callback toggle_cb = [=] (wf::activator_source_t, uint32_t)
    {
        toggle();
        return true;
    };

    wf::effect_hook_t pre_hook = [=] ()
    {
        damage = output->render->get_scheduled_damage();
    };

    wf::effect_hook_t overlay_hook = [=] ()
    {
        wf::framebuffer_t target_fb = output->render->get_target_framebuffer();
        auto og = output->get_relative_geometry();
        wf::region_t output_region(og);
        wf::region_t inverted_damage;

        float r = 0.25 + static_cast <float> (rand()) /( static_cast <float> (RAND_MAX/(0.5)));
        float g = 0.25 + static_cast <float> (rand()) /( static_cast <float> (RAND_MAX/(0.5)));
        float b = 0.25 + static_cast <float> (rand()) /( static_cast <float> (RAND_MAX/(0.5)));
        wf::color_t color{r, g, b, 0.25};

        for (const auto& b : damage)
        {
            wlr_box box{b.x1, b.y1, b.x2 - b.x1, b.y2 - b.y1};
            OpenGL::render_rectangle(box, color, target_fb.get_orthographic_projection());
        }

        OpenGL::render_begin();
        last_buffer.allocate(og.width, og.height);
        OpenGL::render_end();

        if (!damage.empty())
        {
            OpenGL::render_begin(target_fb);
            GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, last_buffer.fb));
            inverted_damage = output_region ^ damage;
            for (const auto& box : inverted_damage)
            {
                GL_CALL(glBlitFramebuffer(
                    box.x1, target_fb.viewport_height - box.y2,
                    box.x2, target_fb.viewport_height - box.y1,
                    box.x1, target_fb.viewport_height - box.y2,
                    box.x2, target_fb.viewport_height - box.y1,
                    GL_COLOR_BUFFER_BIT, GL_LINEAR));
            }
            OpenGL::render_end();
        }

        OpenGL::render_begin(last_buffer);
        GL_CALL(glBindFramebuffer(GL_READ_FRAMEBUFFER, target_fb.fb));
        GL_CALL(glBlitFramebuffer(
            0, 0, target_fb.viewport_width, target_fb.viewport_height,
            0, 0, target_fb.viewport_width, target_fb.viewport_height,
            GL_COLOR_BUFFER_BIT, GL_LINEAR));
        OpenGL::render_end();
    };

    void fini() override
    {
        output->rem_binding(&toggle_cb);
        output->render->rem_effect(&pre_hook);
        output->render->rem_effect(&overlay_hook);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_showrepaint);
