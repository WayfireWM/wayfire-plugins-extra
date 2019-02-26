#include <plugin.hpp>
#include <config.hpp>
#include <output.hpp>
#include <render-manager.hpp>
#include <debug.hpp>

#include <map>

#include <giomm/dbusconnection.h>
#include <giomm/dbuswatchname.h>
#include <giomm/dbusproxy.h>
#include <giomm/init.h>

#include <glibmm/main.h>
#include <glibmm/init.h>

using namespace Gio;
class WayfireAutorotateIIO : public wayfire_plugin_t
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

        for (auto iconnector : integrated_connectors)
        {
            /* In wlroots, the output name is based on the connector */
            if (std::string(output->handle->name)
                .find_first_of(iconnector) != std::string::npos)
            {
                return true;
            }
        }

        return false;
    }

    guint watch_id;
    public:
    effect_hook_t on_frame = [=] ()
    {
        Glib::MainContext::get_default()->iteration(false);
    };

    Glib::RefPtr<Glib::MainLoop> loop;
    void init(wayfire_config *config) override
    {
        if (!is_autorotate_enabled())
            return;

        if (loop)
        {
            log_error ("Unexpected: more than one integrated panel?");
            return;
        }

        Glib::init();
        Gio::init();

        loop = Glib::MainLoop::create(true);
        output->render->add_effect(&on_frame, WF_OUTPUT_EFFECT_PRE);

        watch_id = DBus::watch_name(DBus::BUS_TYPE_SYSTEM, "net.hadess.SensorProxy",
            sigc::mem_fun(this, &WayfireAutorotateIIO::on_iio_appeared),
            sigc::mem_fun(this, &WayfireAutorotateIIO::on_iio_disappeared));
    }

    Glib::RefPtr<DBus::Proxy> iio_proxy;
    void on_iio_appeared(const Glib::RefPtr<DBus::Connection>& conn,
        Glib::ustring name, Glib::ustring owner)
    {
        log_info("iio-sensors appeared, connecting ...");
        iio_proxy = DBus::Proxy::create_sync(conn,
            name, "/net/hadess/SensorProxy", "net.hadess.SensorProxy");

        if (!iio_proxy)
        {
            log_error("Failed to connect to iio-proxy.");
            return;
        }

        iio_proxy->signal_properties_changed().connect_notify(
            sigc::mem_fun(this, &WayfireAutorotateIIO::on_properties_changed));
        iio_proxy->call_sync("ClaimAccelerometer");
    }

    void on_properties_changed(const Gio::DBus::Proxy::MapChangedProperties& properties,
        const std::vector<Glib::ustring>& invalidated)
    {
        update_orientation();
    }

    void update_orientation()
    {
        if (!iio_proxy)
            return;

        Glib::Variant<Glib::ustring> orientation;
        iio_proxy->get_cached_property(orientation, "AccelerometerOrientation");
        log_info("IIO Accelerometer orientation: %s", orientation.get().c_str());

        static const std::map<std::string, wl_output_transform> transform_by_name =
        {
            {"normal", WL_OUTPUT_TRANSFORM_NORMAL},
            {"left-up", WL_OUTPUT_TRANSFORM_270},
            {"right-up", WL_OUTPUT_TRANSFORM_90},
            {"bottom-up", WL_OUTPUT_TRANSFORM_180},
        };

        if (transform_by_name.count(orientation.get()))
        {
            auto new_transform = transform_by_name.find(orientation.get())->second;
            if (output->get_transform() != new_transform)
                output->set_transform(new_transform);
        }
    }

    void on_iio_disappeared(const Glib::RefPtr<DBus::Connection>& conn,
        Glib::ustring name)
    {
        log_info("lost connection to iio-sensors.");
        iio_proxy.reset();
    }

    void fini() override
    {
        /* If loop is NULL, autorotate was disabled for the current output */
        if (!loop)
            return;

        iio_proxy.reset();
        DBus::unwatch_name(watch_id);
        loop->quit();

        output->render->rem_effect(&on_frame);
    }
};

extern "C"
{
    wayfire_plugin_t* newInstance()
    {
        return new WayfireAutorotateIIO;
    }
}
