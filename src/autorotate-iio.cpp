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
    public:

    effect_hook_t on_frame = [=] ()
    {
        Glib::MainContext::get_default()->iteration(false);
    };

    Glib::RefPtr<Glib::MainLoop> loop;
    void init(wayfire_config *config)
    {
        Glib::init();
        Gio::init();

        loop = Glib::MainLoop::create(true);
        output->render->add_effect(&on_frame, WF_OUTPUT_EFFECT_PRE);

        DBus::watch_name(DBus::BUS_TYPE_SYSTEM, "net.hadess.SensorProxy",
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
};

extern "C"
{
    wayfire_plugin_t* newInstance()
    {
        return new WayfireAutorotateIIO;
    }
}
