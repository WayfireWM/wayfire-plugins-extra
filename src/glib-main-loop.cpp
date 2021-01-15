#include <wayfire/singleton-plugin.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/core.hpp>
#include <glibmm/main.h>
#include <giomm/init.h>
#include <glibmm/init.h>
#include <wayfire/util/log.hpp>
#include <wayfire/config.h>
#include <filesystem>
#include <dlfcn.h>

#include <glib-2.0/glib-unix.h>

static gboolean on_wayland_fd_event(gint fd, GIOCondition condition,
    gpointer user_data);

namespace wf
{
class glib_main_loop_t
{
    Glib::RefPtr<Glib::MainLoop> g_loop;

  public:
    glib_main_loop_t()
    {
        // IMPORTANT!
        // Ensure that the .so file for this plugin is never closed, by opening it
        // with dlopen() once more.
        auto path = find_plugin_in_path();
        if (path.empty())
        {
            LOGE("Failed to find libglib-main-loop.so! ",
                "Add it to the WAYFIRE_PLUGIN_PATH.");
            return;
        }

        auto handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (handle == NULL)
        {
            LOGE("Failed to open ", path, ", glib-main-loop cannot work!");
            return;
        }

        LOGI("creating main loop");

        Glib::init();
        Gio::init();

        g_loop = Glib::MainLoop::create();
        wf::get_core().connect_signal("startup-finished", &glib_loop_run);
        wf::get_core().connect_signal("shutdown", &glib_loop_quit);
    }

    void handle_wayland_fd_in(GIOCondition flag)
    {
        if (flag != G_IO_IN)
        {
            LOGE("A problem in the Wayland event loop has been detected!");
            g_loop->quit();
            return;
        }

        wl_display_flush_clients(wf::get_core().display);
        wl_event_loop_dispatch(wf::get_core().ev_loop, 0);
        wl_display_flush_clients(wf::get_core().display);
    }

    wf::signal_connection_t glib_loop_run = [=] (auto)
    {
        auto fd = wl_event_loop_get_fd(wf::get_core().ev_loop);
        g_unix_fd_add(fd, G_IO_IN, on_wayland_fd_event, this);
        g_unix_fd_add(fd, G_IO_ERR, on_wayland_fd_event, this);
        g_unix_fd_add(fd, G_IO_HUP, on_wayland_fd_event, this);

        g_loop->run();
    };

    wf::signal_connection_t glib_loop_quit = [=] (auto)
    {
        auto display = wf::get_core().display;
        wl_display_destroy_clients(display);
        wl_display_destroy(display);
        std::exit(0);
    };

    /**
     * Find the path to this plugin, by searching in Wayfire's search path.
     *
     * Code adapted from plugin-loader.cpp
     */
    std::string find_plugin_in_path()
    {
        std::vector<std::string> plugin_prefixes;
        if (char *plugin_path = getenv("WAYFIRE_PLUGIN_PATH"))
        {
            std::stringstream ss(plugin_path);
            std::string entry;
            while (std::getline(ss, entry, ':'))
            {
                plugin_prefixes.push_back(entry);
            }
        }

        plugin_prefixes.push_back(PLUGIN_PATH);

        std::string plugin_name = "glib-main-loop";
        for (std::filesystem::path plugin_prefix : plugin_prefixes)
        {
            auto plugin_path = plugin_prefix / ("lib" + plugin_name + ".so");
            if (std::filesystem::exists(plugin_path))
            {
                return plugin_path;
            }
        }

        return "";
    }
};
}

static gboolean on_wayland_fd_event(gint, GIOCondition condition,
    gpointer user_data)
{
    auto loop = (wf::glib_main_loop_t*)user_data;
    loop->handle_wayland_fd_in(condition);
    return true;
}

DECLARE_WAYFIRE_PLUGIN((wf::singleton_plugin_t<wf::glib_main_loop_t, true>));
