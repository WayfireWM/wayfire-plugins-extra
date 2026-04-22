#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/input-device.hpp>
#include <wayfire/output-layout.hpp>
#include <wayfire/core.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/signal-definitions.hpp>

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
#include <wayland-server-core.h>
}

using namespace Gio;
class WayfireAutorotateIIO : public wf::per_output_plugin_instance_t
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

    wf::signal::connection_t<wf::input_device_added_signal> on_input_devices_changed = [=] (void*)
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
    wf::plugin_activation_data_t grab_interface{
        .name = "autorotate-iio",
        .capabilities = 0,
    };

    guint watch_id;
    wf::activator_callback on_rotate_left = [=] (auto)
    {
        return on_rotate_binding(WL_OUTPUT_TRANSFORM_90);
    };
    wf::activator_callback on_rotate_right = [=] (auto)
    {
        return on_rotate_binding(WL_OUTPUT_TRANSFORM_270);
    };
    wf::activator_callback on_rotate_up = [=] (auto)
    {
        return on_rotate_binding(WL_OUTPUT_TRANSFORM_NORMAL);
    };
    wf::activator_callback on_rotate_down = [=] (auto)
    {
        return on_rotate_binding(WL_OUTPUT_TRANSFORM_180);
    };

    /* User-specified rotation via keybinding, -1 means not set */
    int32_t user_rotation = -1;

    /* Transform coming from the iio-sensors, -1 means not set */
    int32_t sensor_transform = -1;

    /* Debounce: pending transform waiting to be confirmed stable */
    int32_t pending_transform = -1;
    guint debounce_timer_id = 0;
    static constexpr guint DEBOUNCE_ROTATE_MS = 500;
    static constexpr guint DEBOUNCE_NORMAL_MS = 1200;

    bool on_rotate_binding(int32_t target_rotation)
    {
        if (!output->can_activate_plugin(&grab_interface))
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

    /* Wayland event loop timer — fires every 50ms to pump the GLib main context
     * even when the display is idle and on_frame would not run. */
    wl_event_source *glib_timer = nullptr;

    static int glib_timer_cb(void *data)
    {
        auto *self = static_cast<WayfireAutorotateIIO*>(data);
        Glib::MainContext::get_default()->iteration(false);
        wl_event_source_timer_update(self->glib_timer, 50);
        return 0;
    }

    Glib::RefPtr<Glib::MainLoop> loop;

  public:
    void init() override
    {
        output->add_activator(rotate_left_opt, &on_rotate_left);
        output->add_activator(rotate_right_opt, &on_rotate_right);
        output->add_activator(rotate_up_opt, &on_rotate_up);
        output->add_activator(rotate_down_opt, &on_rotate_down);

        on_input_devices_changed.emit(nullptr);
        wf::get_core().connect(&on_input_devices_changed);

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

        auto *evloop = wl_display_get_event_loop(wf::get_core().display);
        glib_timer = wl_event_loop_add_timer(evloop, glib_timer_cb, this);
        wl_event_source_timer_update(glib_timer, 50);

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
        update_orientation();
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
        if (!orientation)
        {
            return;
        }

        LOGI("IIO Accelerometer orientation: %s", orientation.get().c_str());

        static const std::map<std::string, wl_output_transform> transform_by_name =
        {
            {"normal", WL_OUTPUT_TRANSFORM_NORMAL},
            {"left-up", WL_OUTPUT_TRANSFORM_90},
            {"right-up", WL_OUTPUT_TRANSFORM_270},
            {"bottom-up", WL_OUTPUT_TRANSFORM_180},
        };

        if (!transform_by_name.count(orientation.get()))
        {
            return;
        }

        int32_t new_transform = transform_by_name.find(orientation.get())->second;

        bool timer_running  = (debounce_timer_id != 0);
        bool new_is_normal  = (new_transform == WL_OUTPUT_TRANSFORM_NORMAL);
        bool pend_is_normal = (pending_transform == WL_OUTPUT_TRANSFORM_NORMAL);

        if (!timer_running)
        {
            if (new_transform == sensor_transform)
            {
                return;
            }

            pending_transform = new_transform;
            guint delay = new_is_normal ? DEBOUNCE_NORMAL_MS : DEBOUNCE_ROTATE_MS;
            debounce_timer_id = g_timeout_add(delay, [] (gpointer data) -> gboolean
            {
                auto *self = static_cast<WayfireAutorotateIIO*>(data);
                self->debounce_timer_id = 0;
                self->sensor_transform  = self->pending_transform;
                self->update_transform();
                return G_SOURCE_REMOVE;
            }, this);
        }
        else if (!pend_is_normal)
        {
            if (!new_is_normal)
            {
                pending_transform = new_transform;
            }
        }
        else
        {
            if (!new_is_normal)
            {
                g_source_remove(debounce_timer_id);
                pending_transform = new_transform;
                debounce_timer_id = g_timeout_add(DEBOUNCE_ROTATE_MS, [] (gpointer data) -> gboolean
                {
                    auto *self = static_cast<WayfireAutorotateIIO*>(data);
                    self->debounce_timer_id = 0;
                    self->sensor_transform  = self->pending_transform;
                    self->update_transform();
                    return G_SOURCE_REMOVE;
                }, this);
            }
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

        if (debounce_timer_id)
        {
            g_source_remove(debounce_timer_id);
            debounce_timer_id = 0;
        }

        /* If loop is NULL, autorotate was disabled for the current output */
        if (loop)
        {
            iio_proxy.reset();
            DBus::unwatch_name(watch_id);
            loop->quit();
            wl_event_source_remove(glib_timer);
            glib_timer = nullptr;
        }
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<WayfireAutorotateIIO>);
