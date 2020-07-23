#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/input-device.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/core.hpp>
#include <wayfire/util/log.hpp>

#include <giomm/dbusconnection.h>
#include <giomm/dbuswatchname.h>
#include <giomm/dbusproxy.h>
#include <giomm/init.h>

#include <glibmm/main.h>
#include <glibmm/init.h>

#include <map>

extern "C"
{
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
}

using namespace Gio;
class WayfireAutorotateIIO : public wf::plugin_interface_t
{
    /* Tries to detect whether autorotate is enabled for the current output.
     * Currently it is enabled only for integrated panels */
    bool is_autorotate_enabled()
    {
        static const std::string integrated_connectors[] = {
            "eDP",
            "LVDS",
            "DSI",
        };

        /* In wlroots, the output name is based on the connector */
        auto output_connector = std::string(output->handle->name);
        for (auto iconnector : integrated_connectors)
        {
            if (output_connector.find(iconnector) != iconnector.npos)
            {
                return true;
            }
        }

        return false;
    }

    wf::signal_callback_t on_input_devices_changed = [=] (void*)
    {
        if (!is_autorotate_enabled())
        {
            return;
        }

        auto devices = wf::get_core().get_input_devices();
        for (auto& dev : devices)
        {
            if (dev->get_wlr_handle()->type == WLR_INPUT_DEVICE_TOUCH)
            {
                auto cursor = wf::get_core().get_wlr_cursor();
                wlr_cursor_map_input_to_output(cursor, dev->get_wlr_handle(),
                    output->handle);
            }
        }
    };

    wf::option_wrapper_t<wf::activatorbinding_t>
    rotate_up_opt{"autorotate-iio/rotate_up"},
    rotate_left_opt{"autorotate-iio/rotate_left"},
    rotate_down_opt{"autorotate-iio/rotate_down"},
    rotate_right_opt{"autorotate-iio/rotate_right"};
    wf::option_wrapper_t<bool>
    config_rotation_locked{"autorotate-iio/lock_rotation"};

    guint watch_id;
    wf::activator_callback on_rotate_left = [=] (wf::activator_source_t src, int32_t)
    {
        return on_rotate_binding(WL_OUTPUT_TRANSFORM_270);
    };
    wf::activator_callback on_rotate_right =
        [=] (wf::activator_source_t src, int32_t)
    {
        return on_rotate_binding(WL_OUTPUT_TRANSFORM_90);
    };
    wf::activator_callback on_rotate_up = [=] (wf::activator_source_t src, int32_t)
    {
        return on_rotate_binding(WL_OUTPUT_TRANSFORM_NORMAL);
    };
    wf::activator_callback on_rotate_down = [=] (wf::activator_source_t src, int32_t)
    {
        return on_rotate_binding(WL_OUTPUT_TRANSFORM_180);
    };

    /* User-specified rotation via keybinding, -1 means not set */
    int32_t user_rotation = -1;

    /* Transform coming from the iio-sensors, -1 means not set */
    int32_t sensor_transform = -1;

    bool on_rotate_binding(int32_t target_rotation)
    {
        if (!output->can_activate_plugin(grab_interface))
        {
            return false;
        }

        /* If the user presses the same rotation binding twice, this means
         * unlock the rotation. Otherwise, just use the new rotation. */
        if (target_rotation == user_rotation)
        {
            user_rotation = -1;
        } else
        {
            user_rotation = target_rotation;
        }

        return update_transform();
    }

    /** Calculate the transform based on user and sensor data, and apply it */
    bool update_transform()
    {
        wl_output_transform transform_to_use;
        if (user_rotation >= 0)
        {
            transform_to_use = (wl_output_transform)user_rotation;
        } else if ((sensor_transform >= 0) && !config_rotation_locked)
        {
            transform_to_use = (wl_output_transform)sensor_transform;
        } else
        {
            /* No user rotation set, and no sensor data */
            return false;
        }

        auto configuration =
            wf::get_core().output_layout->get_current_configuration();
        if (configuration[output->handle].transform == transform_to_use)
        {
            return false;
        }

        configuration[output->handle].transform = transform_to_use;
        wf::get_core().output_layout->apply_configuration(configuration);

        return true;
    }

    wf::effect_hook_t on_frame = [=] ()
    {
        Glib::MainContext::get_default()->iteration(false);
    };
    Glib::RefPtr<Glib::MainLoop> loop;

  public:
    void init() override
    {
        output->add_activator(rotate_left_opt, &on_rotate_left);
        output->add_activator(rotate_right_opt, &on_rotate_right);
        output->add_activator(rotate_up_opt, &on_rotate_up);
        output->add_activator(rotate_down_opt, &on_rotate_down);

        on_input_devices_changed(nullptr);
        wf::get_core().connect_signal("input-device-added",
            &on_input_devices_changed);

        init_iio_sensors();
    }

    void init_iio_sensors()
    {
        if (!is_autorotate_enabled())
        {
            return;
        }

        Glib::init();
        Gio::init();

        loop = Glib::MainLoop::create(true);
        output->render->add_effect(&on_frame, wf::OUTPUT_EFFECT_PRE);

        watch_id = DBus::watch_name(DBus::BUS_TYPE_SYSTEM, "net.hadess.SensorProxy",
            sigc::mem_fun(this, &WayfireAutorotateIIO::on_iio_appeared),
            sigc::mem_fun(this, &WayfireAutorotateIIO::on_iio_disappeared));
    }

    Glib::RefPtr<DBus::Proxy> iio_proxy;
    void on_iio_appeared(const Glib::RefPtr<DBus::Connection>& conn,
        Glib::ustring name, Glib::ustring owner)
    {
        LOGI("iio-sensors appeared, connecting ...");
        iio_proxy = DBus::Proxy::create_sync(conn,
            name, "/net/hadess/SensorProxy", "net.hadess.SensorProxy");

        if (!iio_proxy)
        {
            LOGE("Failed to connect to iio-proxy.");

            return;
        }

        iio_proxy->signal_properties_changed().connect_notify(
            sigc::mem_fun(this, &WayfireAutorotateIIO::on_properties_changed));
        iio_proxy->call_sync("ClaimAccelerometer");
    }

    void on_properties_changed(
        const Gio::DBus::Proxy::MapChangedProperties& properties,
        const std::vector<Glib::ustring>& invalidated)
    {
        update_orientation();
    }

    void update_orientation()
    {
        if (!iio_proxy)
        {
            return;
        }

        Glib::Variant<Glib::ustring> orientation;
        iio_proxy->get_cached_property(orientation, "AccelerometerOrientation");
        LOGI("IIO Accelerometer orientation: %s", orientation.get().c_str());

        static const std::map<std::string, wl_output_transform> transform_by_name =
        {
            {"normal", WL_OUTPUT_TRANSFORM_NORMAL},
            {"left-up", WL_OUTPUT_TRANSFORM_270},
            {"right-up", WL_OUTPUT_TRANSFORM_90},
            {"bottom-up", WL_OUTPUT_TRANSFORM_180},
        };

        if (transform_by_name.count(orientation.get()))
        {
            sensor_transform = transform_by_name.find(orientation.get())->second;
            update_transform();
        }
    }

    void on_iio_disappeared(const Glib::RefPtr<DBus::Connection>& conn,
        Glib::ustring name)
    {
        LOGI("lost connection to iio-sensors.");
        iio_proxy.reset();
    }

    void fini() override
    {
        output->rem_binding(&on_rotate_left);
        output->rem_binding(&on_rotate_right);
        output->rem_binding(&on_rotate_up);
        output->rem_binding(&on_rotate_down);

        wf::get_core().disconnect_signal("input-device-added",
            &on_input_devices_changed);

        /* If loop is NULL, autorotate was disabled for the current output */
        if (loop)
        {
            iio_proxy.reset();
            DBus::unwatch_name(watch_id);
            loop->quit();
            output->render->rem_effect(&on_frame);
        }
    }
};

DECLARE_WAYFIRE_PLUGIN(WayfireAutorotateIIO);
