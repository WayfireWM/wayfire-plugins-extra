#include <wayfire/signal-definitions.hpp>
#include <wayfire/singleton-plugin.hpp>

class JoinViewsSingleton
{
  public:
    wf::signal_connection_t on_geometry_changed{[=] (wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);
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

    wf::signal_connection_t on_view_map{[=] (wf::signal_data_t *data)
        {
            auto view = get_signaled_view(data);
            /* Make sure only a single connection is made */
            view->disconnect_signal(&on_geometry_changed);
            view->connect_signal("geometry-changed", &on_geometry_changed);
        }
    };

    void handle_new_output(wf::output_t *output)
    {
        output->connect_signal("view-mapped", &on_view_map);
    }

    JoinViewsSingleton()
    {
        wf::option_wrapper_t<bool> opt{"move/join_views"};
        ((wf::option_sptr_t<bool>)opt)->set_value(true);
    }
};

class JoinViews : public wf::singleton_plugin_t<JoinViewsSingleton>
{
  public:
    virtual void init() override
    {
        singleton_plugin_t::init();
        get_instance().handle_new_output(output);
    }
};

DECLARE_WAYFIRE_PLUGIN(JoinViews);
