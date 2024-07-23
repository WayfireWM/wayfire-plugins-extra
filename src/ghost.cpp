/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 Scott Moreau
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
#include <wayfire/matcher.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/ipc/ipc-activator.hpp>

const std::string ghost_transformer_name = "ghost_transformer";

namespace wf
{
namespace ghost
{
using namespace wf::scene;
class ghost_view : public wf::scene::view_2d_transformer_t
{
  public:

    ghost_view(wayfire_view view) : wf::scene::view_2d_transformer_t(view)
    {}

    std::optional<wf::scene::input_node_t> find_node_at(const wf::pointf_t& at) override
    {
        return {};
    }

    virtual ~ghost_view()
    {}
};

class ghost_plugin : public wf::plugin_interface_t
{
    wf::ipc_activator_t ghost_toggle{"ghost/ghost_toggle"};
    wf::view_matcher_t ghost_match{"ghost/ghost_match"};

    void pop_transformer(wayfire_view view)
    {
        if (view->get_transformed_node()->get_transformer(ghost_transformer_name))
        {
            view->get_transformed_node()->rem_transformer(ghost_transformer_name);
        }
    }

    void remove_shade_transformers()
    {
        for (auto& view : wf::get_core().get_all_views())
        {
            pop_transformer(view);
        }
    }

    std::shared_ptr<wf::ghost::ghost_view> ensure_transformer(wayfire_view view)
    {
        auto tmgr = view->get_transformed_node();
        if (auto tr = tmgr->get_transformer<wf::ghost::ghost_view>(ghost_transformer_name))
        {
            return tr;
        }

        auto node = std::make_shared<wf::ghost::ghost_view>(view);
        tmgr->add_transformer(node, wf::TRANSFORMER_2D, ghost_transformer_name);
        auto tr = tmgr->get_transformer<wf::ghost::ghost_view>(ghost_transformer_name);

        return tr;
    }

  public:

    void init() override
    {
        for (auto& view : wf::get_core().get_all_views())
        {
            if (ghost_match.matches(view))
            {
                ensure_transformer(view);
            }
        }

        ghost_toggle.set_handler(ghost_view_toggle_cb);
        wf::get_core().connect(&on_view_map);
    }

    wf::ipc_activator_t::handler_t ghost_view_toggle_cb = [=] (wf::output_t *output, wayfire_view view)
    {
        auto top_view = wf::get_core().seat->get_active_view();
        if (top_view && top_view->get_transformed_node()->get_transformer(ghost_transformer_name))
        {
            pop_transformer(top_view);
            return true;
        }

        if (!view)
        {
            return false;
        }

        if (view->get_transformed_node()->get_transformer(ghost_transformer_name))
        {
            pop_transformer(view);
            return true;
        }

        ensure_transformer(view);
        return true;
    };

    wf::signal::connection_t<wf::view_mapped_signal> on_view_map = [=] (wf::view_mapped_signal *ev)
    {
        if (ghost_match.matches(ev->view))
        {
            ensure_transformer(ev->view);
        }
    };

    void fini() override
    {
        remove_shade_transformers();
        on_view_map.disconnect();
    }
};
}
}

DECLARE_WAYFIRE_PLUGIN(wf::ghost::ghost_plugin);
