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
#include <wayfire/core.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/signal-definitions.hpp>

class FullscreenTransformer : public wf::view_2D
{
    public:

    FullscreenTransformer(wayfire_view view) : wf::view_2D(view) { }

    ~FullscreenTransformer() { }

    void render_box(wf::texture_t src_tex, wlr_box src_box,
        wlr_box scissor_box, const wf::framebuffer_t& target_fb) override
    {
        OpenGL::render_begin(target_fb);
        target_fb.scissor(scissor_box);
        OpenGL::clear({0, 0, 0, 1});
        OpenGL::render_end();

        wf::view_2D::render_box(src_tex, src_box, scissor_box, target_fb);
    }
};

class wayfire_fullscreen : public wf::plugin_interface_t
{
  nonstd::observer_ptr<wf::view_interface_t> our_view;
  std::string transformer_name;
  FullscreenTransformer *our_transform = nullptr;
  bool fullscreen;
  wf::geometry_t saved_geometry;
  wf::option_wrapper_t<wf::keybinding_t> key_toggle_fullscreen{"fullscreen/key_toggle_fullscreen"};
  wf::option_wrapper_t<bool> preserve_aspect{"fullscreen/preserve_aspect"};

  public:
    void init() override
    {
        this->grab_interface->name = "fullscreen";
        this->grab_interface->capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR;
        transformer_name = this->grab_interface->name;

        output->add_key(key_toggle_fullscreen, &on_toggle_fullscreen);
        fullscreen = false;
    }

    void setup_transform(FullscreenTransformer *transform, wf::geometry_t output_geometry,
        wf::geometry_t view_geometry)
    {
        auto og = output_geometry;
        auto vg = view_geometry;

        our_transform->scale_x = (double) og.width / vg.width;
        our_transform->scale_y = (double) og.height / vg.height;
        our_transform->translation_x = (og.width - vg.width) / 2.0;
        our_transform->translation_y = (og.height - vg.height) / 2.0;

        if (!preserve_aspect)
            return;

        if (our_transform->scale_x > our_transform->scale_y)
            our_transform->scale_x = our_transform->scale_y;
        else if (our_transform->scale_x < our_transform->scale_y)
            our_transform->scale_y = our_transform->scale_x;
    }

    wf::key_callback on_toggle_fullscreen = [=] (uint32_t key)
    {
        auto view = output->get_active_view();
        if (!view)
            return false;

        if (output->activate_plugin(grab_interface))
        {
            fullscreen = !fullscreen;

            if (fullscreen)
                saved_geometry = view->get_wm_geometry();

            view->set_fullscreen(fullscreen);
	    
            if (!fullscreen)
            {
                deactivate(view);
                view_unmapped.disconnect();
                output->deactivate_plugin(grab_interface);
                return true;
            }

            activate(view);

            return true;
        }

        return false;
    };

    void activate(nonstd::observer_ptr<wf::view_interface_t> view)
    {
        auto og = output->get_relative_geometry();
        auto vg = view->get_wm_geometry();
        view->move(0, 0);
        our_transform = new FullscreenTransformer(view);
        setup_transform(our_transform, og, vg);
        view->add_transformer(std::unique_ptr<FullscreenTransformer> (our_transform), transformer_name);
        view->connect_signal("geometry-changed", &view_geometry_changed);
        output->connect_signal("view-fullscreen-request", &view_fullscreened);
        output->connect_signal("unmap-view", &view_unmapped);
        view->damage();
        our_view = view;

        output->deactivate_plugin(grab_interface);
    }

    void deactivate(nonstd::observer_ptr<wf::view_interface_t> view)
    {
        view->move(saved_geometry.x, saved_geometry.y);
        if (view->get_transformer(transformer_name))
            view->pop_transformer(transformer_name);
        view_geometry_changed.disconnect();
        view_fullscreened.disconnect();
        fullscreen = false;
        our_view = nullptr;
    }

    wf::signal_connection_t view_unmapped{[this] (wf::signal_data_t *data)
    {
        auto view = get_signaled_view(data);

        if (view != our_view)
            return;

        view->set_fullscreen(false);
        deactivate(view);
    }};

    wf::signal_connection_t view_fullscreened{[this] (wf::signal_data_t *data)
    {
        auto conv = static_cast<view_fullscreen_signal*> (data);
        assert(conv);

        if (conv->view != our_view)
            return;

        if (conv->state || conv->carried_out)
            return;

        deactivate(conv->view);

        conv->carried_out = true;
    }};

    wf::signal_connection_t view_geometry_changed{[this] (wf::signal_data_t *data)
    {
        auto view = get_signaled_view(data);

        if (view != our_view)
            return;

        auto og = output->get_relative_geometry();
        auto vg = view->get_wm_geometry();

        setup_transform(our_transform, og, vg);
        view->damage();
    }};

    void fini() override
    {
        output->rem_binding(&on_toggle_fullscreen);
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_fullscreen);
