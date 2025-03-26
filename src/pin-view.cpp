/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 Scott Moreau <oreaus@gmail.com>
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
#include <wayfire/view-helpers.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>

namespace wf
{
namespace pin_view
{
class pin_view_data : public wf::custom_data_t
{
  public:
    wf::geometry_t geometry;
    wf::point_t workspace;
    wf::view_role_t role;
};
class wayfire_pin_view : public wf::plugin_interface_t
{
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc_repo;

  public:
    void init() override
    {
        ipc_repo->register_method("pin-view/pin", ipc_pin_view);
        ipc_repo->register_method("pin-view/unpin", ipc_unpin_view);
        for (auto & output : wf::get_core().output_layout->get_outputs())
        {
            output->connect(&on_workspace_changed);
        }
    }

    wf::ipc::method_callback ipc_pin_view = [=] (wf::json_t data) -> wf::json_t
    {
        auto view_id = wf::ipc::json_get_uint64(data, "view-id");
        auto layer_str = wf::ipc::json_get_string(data, "layer");
        auto resize = wf::ipc::json_get_bool(data, "resize");
        /* workspace x,y */
        auto optional_x = wf::ipc::json_get_optional_uint64(data, "x");
        auto optional_y = wf::ipc::json_get_optional_uint64(data, "y");

        auto view = wf::ipc::find_view_by_id(view_id);
        if (view)
        {
            bool was_pinned = unpin(view);
            auto output     = view->get_output();
            if (!view->get_data<pin_view_data>())
            {
                pin_view_data pv_data;
                pv_data.role = view->role;

                if (auto toplevel = toplevel_cast(view))
                {
                    if (!was_pinned)
                    {
                        pv_data.workspace = output->wset()->get_view_main_workspace(toplevel);
                        pv_data.geometry  = toplevel->get_geometry();
                    }
                }

                view->store_data(std::make_unique<pin_view_data>(pv_data));
            }

            wf::scene::layer layer;
            if (layer_str == "background")
            {
                layer = wf::scene::layer::BACKGROUND;
            } else if (layer_str == "bottom")
            {
                layer = wf::scene::layer::BOTTOM;
            } else if (layer_str == "workspace")
            {
                layer = wf::scene::layer::WORKSPACE;
            } else if (layer_str == "top")
            {
                layer = wf::scene::layer::TOP;
            } else if (layer_str == "unmanaged")
            {
                layer = wf::scene::layer::UNMANAGED;
            } else if (layer_str == "overlay")
            {
                layer = wf::scene::layer::OVERLAY;
            } else if (layer_str == "lock")
            {
                layer = wf::scene::layer::LOCK;
            } else
            {
                layer = wf::scene::layer::TOP;
            }

            auto pvd = view->get_data<pin_view_data>();
            auto og = output->get_relative_geometry();
            int x = 0, y = 0;
            wf::view_unmapped_signal unmap_signal;
            unmap_signal.view = view;
            wf::get_core().emit(&unmap_signal);
            if (optional_x.has_value())
            {
                x = optional_x.value();
                if (optional_y.has_value())
                {
                    y = optional_y.value();
                }

                view->role = pvd->role;

                wf::point_t nws{x, y};
                if (auto toplevel = toplevel_cast(view))
                {
                    auto cws = output->wset()->get_view_main_workspace(toplevel);
                    if (resize)
                    {
                        toplevel->set_geometry(wf::geometry_t{(nws.x - cws.x) * og.width,
                            (nws.y - cws.y) * og.height, og.width, og.height});
                    } else
                    {
                        auto vg = was_pinned ? pvd->geometry : toplevel->get_geometry();
                        toplevel->set_geometry(wf::geometry_t{vg.x + (nws.x - cws.x) * og.width,
                            vg.y + (nws.y - cws.y) * og.height, vg.width, vg.height});
                    }

                    output->wset()->remove_view(toplevel);
                }
            } else
            {
                if (auto toplevel = toplevel_cast(view))
                {
                    auto vg = was_pinned ? pvd->geometry : toplevel->get_geometry();
                    wf::point_t nws = output->wset()->get_current_workspace();
                    auto cws = output->wset()->get_view_main_workspace(toplevel);
                    toplevel->move(vg.x + (nws.x - cws.x) * og.width, vg.y + (nws.y - cws.y) * og.height);
                    if (resize)
                    {
                        toplevel->set_geometry(og);
                    }

                    output->wset()->remove_view(toplevel);
                }

                view->role = wf::VIEW_ROLE_DESKTOP_ENVIRONMENT;
            }

            wf::scene::readd_front(output->node_for_layer(layer), view->get_root_node());
        } else
        {
            return wf::ipc::json_error("Failed to find view with given id.");
        }

        return wf::ipc::json_ok();
    };

    bool unpin(wayfire_view view)
    {
        if (view && view->get_data<pin_view_data>())
        {
            auto pvd = view->get_data<pin_view_data>();
            view->role = pvd->role;
            wf::scene::readd_front(view->get_output()->wset()->get_node(), view->get_root_node());
            if (auto toplevel = toplevel_cast(view))
            {
                auto output = view->get_output();
                output->wset()->add_view(toplevel);
                toplevel->set_geometry(pvd->geometry);
            }

            wf::view_mapped_signal map_signal;
            map_signal.view = view;
            wf::get_core().emit(&map_signal);
            return true;
        }

        return false;
    }

    wf::ipc::method_callback ipc_unpin_view = [=] (wf::json_t data) -> wf::json_t
    {
        auto view_id = wf::ipc::json_get_uint64(data, "view-id");

        auto view = wf::ipc::find_view_by_id(view_id);
        if (!unpin(view))
        {
            LOGE("Failed to find view with given id. Perhaps it is not pinned.");
            return wf::ipc::json_error("Failed to find view with given id. Perhaps it is not pinned.");
        }

        view->release_data<pin_view_data>();

        return wf::ipc::json_ok();
    };

    wf::signal::connection_t<wf::workspace_changed_signal> on_workspace_changed =
        [=] (wf::workspace_changed_signal *ev)
    {
        auto ows    = ev->old_viewport;
        auto nws    = ev->new_viewport;
        auto output = ev->output;
        auto og     = output->get_relative_geometry();
        for (auto & view : wf::get_core().get_all_views())
        {
            auto pvd = view->get_data<pin_view_data>();
            if (!pvd)
            {
                continue;
            }

            if (auto toplevel = toplevel_cast(view))
            {
                auto vg = toplevel->get_geometry();
                if (view->role == wf::VIEW_ROLE_DESKTOP_ENVIRONMENT)
                {
                    auto cws = output->wset()->get_view_main_workspace(toplevel);
                    toplevel->move(vg.x + (nws.x - cws.x) * og.width, vg.y + (nws.y - cws.y) * og.height);
                } else
                {
                    toplevel->move(vg.x + (ows.x - nws.x) * og.width, vg.y + (ows.y - nws.y) * og.height);
                }
            }
        }
    };

    void fini() override
    {
        for (auto & view : wf::get_core().get_all_views())
        {
            if (view->get_data<pin_view_data>())
            {
                unpin(view);
                view->release_data<pin_view_data>();
            }
        }

        ipc_repo->unregister_method("pin-view/pin");
        ipc_repo->unregister_method("pin-view/unpin");
        on_workspace_changed.disconnect();
    }
};
}
}

DECLARE_WAYFIRE_PLUGIN(wf::pin_view::wayfire_pin_view);
