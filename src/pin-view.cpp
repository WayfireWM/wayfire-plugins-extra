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
    wf::view_role_t role, current_role;
};
class wayfire_pin_view : public wf::plugin_interface_t
{
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc_repo;

  public:
    void init() override
    {
        ipc_repo->register_method("pin-view/pin", ipc_pin_view);
        ipc_repo->register_method("pin-view/unpin", ipc_unpin_view);
    }

    wf::ipc::method_callback ipc_pin_view = [=] (nlohmann::json data) -> nlohmann::json
    {
        WFJSON_EXPECT_FIELD(data, "view-id", number_unsigned);
        WFJSON_EXPECT_FIELD(data, "layer", string);
        /* workspace x,y */
        WFJSON_OPTIONAL_FIELD(data, "x", number_unsigned);
        WFJSON_OPTIONAL_FIELD(data, "y", number_unsigned);

        auto view = wf::ipc::find_view_by_id(data["view-id"]);
        if (view)
        {
            auto output = view->get_output();
            output->connect(&on_workspace_changed);
            if (!view->get_data<pin_view_data>())
            {
                pin_view_data pv_data;
                pv_data.current_role = pv_data.role = view->role;
                if (!data.contains("x"))
                {
                    pv_data.current_role = wf::VIEW_ROLE_DESKTOP_ENVIRONMENT;
                }

                if (auto toplevel = toplevel_cast(view))
                {
                    pv_data.workspace = output->wset()->get_view_main_workspace(toplevel);
                    pv_data.geometry  = toplevel->get_geometry();
                }

                view->store_data(std::make_unique<pin_view_data>(pv_data));
            }

            wf::scene::layer layer;
            if (data["layer"] == "background")
            {
                layer = wf::scene::layer::BACKGROUND;
            } else if (data["layer"] == "bottom")
            {
                layer = wf::scene::layer::BOTTOM;
            } else if (data["layer"] == "workspace")
            {
                layer = wf::scene::layer::WORKSPACE;
            } else if (data["layer"] == "top")
            {
                layer = wf::scene::layer::TOP;
            } else if (data["layer"] == "unmanaged")
            {
                layer = wf::scene::layer::UNMANAGED;
            } else if (data["layer"] == "overlay")
            {
                layer = wf::scene::layer::OVERLAY;
            } else if (data["layer"] == "lock")
            {
                layer = wf::scene::layer::LOCK;
            } else
            {
                layer = wf::scene::layer::TOP;
            }

            auto og = output->get_relative_geometry();
            int x = 0, y = 0;
            if (data.contains("x"))
            {
                x = data["x"].get<int>();
                if (data.contains("y"))
                {
                    y = data["y"].get<int>();
                }

                wf::point_t nws{x, y};
                if (auto toplevel = toplevel_cast(view))
                {
                    auto cws = output->wset()->get_view_main_workspace(toplevel);
                    toplevel->set_geometry(wf::geometry_t{(nws.x - cws.x) * og.width,
                        (nws.y - cws.y) * og.height, og.width, og.height});
                }
            } else
            {
                if (auto toplevel = toplevel_cast(view))
                {
                    wf::point_t nws = output->wset()->get_current_workspace();
                    auto cws = output->wset()->get_view_main_workspace(toplevel);
                    auto vg  = toplevel->get_geometry();
                    toplevel->move(vg.x + (nws.x - cws.x) * og.width, vg.y + (nws.y - cws.y) * og.height);
                    toplevel->set_geometry(og);
                }

                view->role = wf::VIEW_ROLE_DESKTOP_ENVIRONMENT;
                wf::scene::readd_front(view->get_output()->node_for_layer(layer), view->get_root_node());
                return wf::ipc::json_ok();
            }

            wf::scene::readd_front(output->node_for_layer(layer), view->get_root_node());
        } else
        {
            return wf::ipc::json_error("Failed to find view with given id.");
        }

        return wf::ipc::json_ok();
    };

    wf::ipc::method_callback ipc_unpin_view = [=] (nlohmann::json data) -> nlohmann::json
    {
        WFJSON_EXPECT_FIELD(data, "view-id", number_unsigned);

        auto view = wf::ipc::find_view_by_id(data["view-id"]);
        if (view && view->get_data<pin_view_data>())
        {
            auto pvd = view->get_data<pin_view_data>();
            view->role = pvd->role;
            wf::scene::readd_front(view->get_output()->wset()->get_node(), view->get_root_node());
            if (auto toplevel = toplevel_cast(view))
            {
                auto output = view->get_output();
                auto og     = output->get_relative_geometry();
                auto cws    = output->wset()->get_view_main_workspace(toplevel);
                auto vg     = toplevel->get_geometry();
                toplevel->move(vg.x + (pvd->workspace.x - cws.x) * og.width,
                    vg.y + (pvd->workspace.y - cws.y) * og.height);
                toplevel->set_geometry(pvd->geometry);
            }

            view->release_data<pin_view_data>();
        } else
        {
            LOGE("Failed to find view with given id. Perhaps it is not pinned.");
            return wf::ipc::json_error("Failed to find view with given id. Perhaps it is not pinned.");
        }

        return wf::ipc::json_ok();
    };

    wf::signal::connection_t<wf::workspace_changed_signal> on_workspace_changed =
        [=] (wf::workspace_changed_signal *ev)
    {
        auto nws    = ev->new_viewport;
        auto output = ev->output;
        auto og     = output->get_relative_geometry();
        for (auto & view : wf::get_core().get_all_views())
        {
            auto pvd = view->get_data<pin_view_data>();
            if (!pvd || (pvd->current_role != wf::VIEW_ROLE_DESKTOP_ENVIRONMENT))
            {
                continue;
            }

            if (auto toplevel = toplevel_cast(view))
            {
                auto cws = output->wset()->get_view_main_workspace(toplevel);
                auto vg  = toplevel->get_geometry();
                toplevel->move(vg.x + (nws.x - cws.x) * og.width, vg.y + (nws.y - cws.y) * og.height);
            }
        }
    };

    void fini() override
    {
        for (auto & view : wf::get_core().get_all_views())
        {
            if (view->get_data<pin_view_data>())
            {
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
