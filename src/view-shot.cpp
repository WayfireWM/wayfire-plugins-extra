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
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/view.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/img.hpp>
#include <wayfire/bindings-repository.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>

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

class wayfire_view_shot : public wf::plugin_interface_t
{
    const std::string transformer_name = "view_shot";
    wf::option_wrapper_t<wf::activatorbinding_t> capture_binding{"view-shot/capture"};
    wf::option_wrapper_t<std::string> file_name{"view-shot/filename"};
    wf::option_wrapper_t<std::string> command{"view-shot/command"};
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc_repo;

  public:
    void init() override
    {
        wf::get_core().bindings->add_activator(capture_binding, &on_capture);
        ipc_repo->register_method("view-shot/capture", on_ipc_capture);
    }

    wf::activator_callback on_capture = [=] (auto)
    {
        auto view = wf::get_core().get_cursor_focus_view();

        if (!view)
        {
            return false;
        }

        char _file_name[255];
        auto time = std::time(nullptr);
        std::strftime(_file_name, sizeof(_file_name),
            file_name.value().c_str(), std::localtime(&time));
        std::string formatted_file_name = _file_name;

        if (take_snapshot(view, formatted_file_name))
        {
            wf::get_core().run(replaceAll(command, "%f", formatted_file_name));
            return true;
        }

        return false;
    };

    wf::ipc::method_callback on_ipc_capture = [=] (wf::json_t data)
    {
        auto view_id = wf::ipc::json_get_uint64(data, "view-id");
        auto file    = wf::ipc::json_get_string(data, "file");

        auto view = wf::ipc::find_view_by_id(view_id);
        if (!view)
        {
            return wf::ipc::json_error("No such view found!");
        }

        if (take_snapshot(view, file))
        {
            return wf::ipc::json_ok();
        }

        return wf::ipc::json_error("Failed to capture view.");
    };

    bool take_snapshot(wayfire_view view, std::string filename)
    {
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

        image_io::write_to_file(filename, pixels, width, height, "png", true);
        free(pixels);
        return true;
    }

    void fini() override
    {
        wf::get_core().bindings->rem_binding(&on_capture);
        ipc_repo->unregister_method("view-shot/capture");
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_view_shot);
