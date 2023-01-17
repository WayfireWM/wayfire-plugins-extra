#include <wayfire/signal-definitions.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>

class JoinViewsSingleton
{
  public:
    wf::signal::connection_t<wf::view_geometry_changed_signal> on_geometry_changed{[=
        ] (wf::view_geometry_changed_signal *ev)
        {
            auto view = ev->view;
            if (!view->is_mapped())
            {
                return;
            }

            auto parent_geometry = view->get_wm_geometry();
            int cx = parent_geometry.x + parent_geometry.width / 2;
            int cy = parent_geometry.y + parent_geometry.height / 2;
            for (auto child : view->children)
            {
                auto target = child->get_wm_geometry();
                target.x = cx - target.width / 2;
                target.y = cy - target.height / 2;
                child->set_geometry(target);
            }
        }
    };

    wf::signal::connection_t<wf::view_mapped_signal> on_view_map{[=] (wf::view_mapped_signal *ev)
        {
            auto view = ev->view;
            /* Make sure only a single connection is made */
            on_geometry_changed.disconnect();
            view->connect(&on_geometry_changed);
        }
    };

    void handle_new_output(wf::output_t *output)
    {
        output->connect(&on_view_map);
    }

    JoinViewsSingleton()
    {
        wf::option_wrapper_t<bool> opt{"move/join_views"};
        ((wf::option_sptr_t<bool>)opt)->set_value(true);
    }
};

class JoinViews : public wf::per_output_plugin_instance_t
{
    wf::shared_data::ref_ptr_t<JoinViewsSingleton> global_idle;

  public:
    virtual void init() override
    {
        global_idle->handle_new_output(output);
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<JoinViews>);
