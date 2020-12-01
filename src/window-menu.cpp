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


#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/workspace-manager.hpp>
#include <wayfire/output-layout.hpp>

class wayfire_window_menu : public wf::plugin_interface_t
{
    /* The command should be set to a client that shows a menu window. */
    wf::option_wrapper_t<std::string> command{"window-menu/command"};
    wf::option_wrapper_t<std::string> app_id{"window-menu/app_id"};
    wf::point_t position_offset;
    wayfire_view menu_view   = nullptr;
    wayfire_view target_view = nullptr;

  public:
    void init() override
    {
        grab_interface->name = "window-menu";
        grab_interface->capabilities = 0;

        output->connect_signal("view-show-window-menu", &show_window_menu);
    }

    wf::signal_connection_t view_mapped{[this] (wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);
            if (target_view && (view->get_app_id() == std::string(app_id)))
            {
                view->set_decoration(nullptr);

                auto vg = target_view->get_output_geometry();
                auto position = wf::point_t{vg.x, vg.y} + position_offset;

                vg = view->get_wm_geometry();
                auto og     = output->get_relative_geometry();
                int padding = 20;
                og.x     += padding;
                og.y     += padding;
                og.width -= padding * 2 + vg.width;
                og.height -= padding * 2 + vg.height;
                if ((og.width <= 0) || (og.height <= 0))
                {
                    return;
                }

                wlr_box box{og.x, og.y, og.width, og.height};
                wf::pointf_t p{(float)position.x, (float)position.y};
                p = target_view->transform_point(p);
                wlr_box_closest_point(&box, p.x, p.y, &p.x, &p.y);
                position.x = (int)p.x;
                position.y = (int)p.y;

                ((wf::view_mapped_signal*)data)->is_positioned = true;
                view->move(position.x, position.y);

                /* Place above other views */
                output->workspace->add_view(view, wf::LAYER_UNMANAGED);

                menu_view = view;
            }
        }
    };

    wf::signal_connection_t on_button{[=] (wf::signal_data_t *data)
        {
            auto ev = static_cast<
                wf::input_event_signal<wlr_event_pointer_button>*>(data);

            if (ev->event->state != WLR_BUTTON_PRESSED)
            {
                return;
            }

            auto view = wf::get_core().get_cursor_focus_view();
            if (!menu_view || !view)
            {
                return;
            }

            /* Check if the client of the views match, in case it's a
             * subsurface/menu, meaning the views won't match but
             * the underlying client object will */
            if (menu_view->get_client() != view->get_client())
            {
                menu_view->close();
            }
        }
    };

    wf::signal_connection_t view_unmapped{[this] (wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);
            if (view && (menu_view == view))
            {
                menu_view = target_view = nullptr;
                output->disconnect_signal(&view_mapped);
                output->disconnect_signal(&view_unmapped);
                wf::get_core().disconnect_signal(&on_button);
            }
        }
    };

    wf::signal_connection_t show_window_menu{[this] (wf::signal_data_t *data)
        {
            if (target_view || menu_view)
            {
                return;
            }

            position_offset =
                ((wf::view_show_window_menu_signal*)data)->relative_position;
            if (wf::get_core().run(std::string(command)) == -1)
            {
                return;
            }

            /* Showing menu for this view */
            target_view = get_signaled_view(data);
            output->connect_signal("view-mapped", &view_mapped);
            output->connect_signal("view-unmapped", &view_unmapped);
            wf::get_core().connect_signal("pointer_button", &on_button);
        }
    };

    void fini() override
    {
        if (menu_view)
        {
            menu_view->close();
        }
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_window_menu);
