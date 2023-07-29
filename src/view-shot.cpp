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
#include <wayfire/opengl.hpp>
#include <wayfire/img.hpp>
#include <wayfire/per-output-plugin.hpp>

#include <ctime>

static std::string replaceAll(std::string s, const std::string& from,
    const std::string& to)
{
    for (unsigned i = 0; i < s.size();)
    {
        auto pos = s.find(from, i);
        if (pos == std::string::npos)
        {
            return s;
        }

        s.replace(pos, from.size(), to);
        i = pos + to.size();
    }

    return s;
}

class wayfire_view_shot : public wf::per_output_plugin_instance_t
{
    const std::string transformer_name = "view_shot";
    wf::option_wrapper_t<wf::activatorbinding_t> capture_binding{"view-shot/capture"};
    wf::option_wrapper_t<std::string> file_name{"view-shot/filename"};
    wf::option_wrapper_t<std::string> command{"view-shot/command"};

  public:
    void init() override
    {
        output->add_activator(capture_binding, &on_capture);
    }

    wf::activator_callback on_capture = [=] (auto)
    {
        auto view = wf::get_core().get_cursor_focus_view();

        if (!view)
        {
            return false;
        }

        wf::render_target_t offscreen_buffer;
        view->take_snapshot(offscreen_buffer);
        auto width  = offscreen_buffer.viewport_width;
        auto height = offscreen_buffer.viewport_height;

        GLubyte *pixels = (GLubyte*)malloc(width * height * sizeof(GLubyte) * 4);
        if (!pixels)
        {
            return false;
        }

        OpenGL::render_begin();
        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, offscreen_buffer.fb));
        GL_CALL(glViewport(0, 0, width, height));

        GL_CALL(glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels));
        offscreen_buffer.release(); // free gpu memory
        OpenGL::render_end();

        char _file_name[255];
        auto time = std::time(nullptr);
        std::strftime(_file_name, sizeof(_file_name),
            file_name.value().c_str(), std::localtime(&time));
        std::string formatted_file_name = _file_name;

        image_io::write_to_file(formatted_file_name, pixels, width, height, "png", true);
        free(pixels);

        wf::get_core().run(replaceAll(command, "%f", formatted_file_name));

        return true;
    };

    void fini() override
    {}
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<wayfire_view_shot>);
