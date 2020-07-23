#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <iostream>

#include <linux/input-event-codes.h>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <gtkmm.h>
#include <memory>

#include <gdk/gdkwayland.h>
#include <wlr-input-inhibitor-unstable-v1-client-protocol.h>

using boost::asio::ip::tcp;

static zwlr_input_inhibitor_v1 *screen_lock = nullptr;
static zwlr_input_inhibit_manager_v1 *inhibitor_manager;
static int server_port = 12345;

/**
 * A class which aggregates all keyboard events from all windows
 * and sends them over the network
 */
class KeyServer
{
  public:

    /** Get the instance of KeyServer */
    static KeyServer& get()
    {
        static KeyServer instance(server_port);

        return instance;
    }

    /**
     * Write data to the socket.
     * We currently send the time, the raw keycode and state over the network.
     * Exact format:
     *
     * <time> <keycode> <state>$
     */
    void handle_key(uint32_t time, uint32_t keycode, uint32_t state)
    {
        /* Check whether to exit */
        update_modifiers(keycode, state);
        if ((keycode == KEY_Q) && state && has_ctrl && has_alt && has_shift)
        {
            Gtk::Application::get_default()->quit();

            return;
        }

        if (!connection_alive)
        {
            return;
        }

        std::string data =
            std::to_string(time) + " " +
            std::to_string(keycode) + " " +
            std::to_string(!!state) + "$";

        boost::system::error_code error;
        boost::asio::write(socket, boost::asio::buffer(data), error);

        /* Wait until next connection */
        if (error)
        {
            connection_alive = false;
            ensure_connection();
        }
    }

  private:
    bool has_ctrl  = false;
    bool has_shift = false;
    bool has_alt   = false;

    void update_modifiers(uint32_t key, uint32_t state)
    {
        if ((key == KEY_LEFTCTRL) || (key == KEY_RIGHTCTRL))
        {
            this->has_ctrl = state;
        }

        if ((key == KEY_LEFTALT) || (key == KEY_RIGHTALT))
        {
            this->has_alt = state;
        }

        if ((key == KEY_LEFTSHIFT) || (key == KEY_RIGHTSHIFT))
        {
            this->has_shift = state;
        }
    }

    static std::unique_ptr<KeyServer> instance;

    bool connection_alive = false;
    void ensure_connection()
    {
        assert(!connection_alive);

        if (socket.is_open())
        {
            socket.close();
        }

        /* Unlock screen while waiting for next connection */
        if (screen_lock)
        {
            zwlr_input_inhibitor_v1_destroy(screen_lock);
            wl_display_flush(
                gdk_wayland_display_get_wl_display(gdk_display_get_default()));
        }

        acceptor.accept(socket);

        /* Lock input */
        if (inhibitor_manager)
        {
            screen_lock =
                zwlr_input_inhibit_manager_v1_get_inhibitor(inhibitor_manager);
        }

        /* Skip all keypresses in the meantime */
        wl_display_roundtrip(
            gdk_wayland_display_get_wl_display(gdk_display_get_default()));

        connection_alive = true;
    }

    boost::asio::io_context io_ctx;
    tcp::acceptor acceptor;
    tcp::socket socket;

    KeyServer(int port) :
        io_ctx(),
        acceptor(io_ctx, tcp::endpoint(tcp::v4(), port)),
        socket(io_ctx)
    {
        ensure_connection();
    }
};

class ServerWindow : public Gtk::Window
{
    GtkEventController *keycontroller;

  public:
    /**
     * @monitor The monitor id on the default screen.
     */
    ServerWindow()
    {
        this->fullscreen();

        /* xkb_common introduces an offset of 8 to key codes, we need to
         * subtract it back to get the real keycode */
#define HW_OFFSET 8

        this->signal_key_press_event().connect_notify([=] (GdkEventKey *ev)
        {
            KeyServer::get().handle_key(ev->time,
                ev->hardware_keycode - HW_OFFSET, 1);
        });

        this->signal_key_release_event().connect_notify([=] (GdkEventKey *ev)
        {
            KeyServer::get().handle_key(ev->time,
                ev->hardware_keycode - HW_OFFSET, 0);
        });
    }
};

// listeners
static void registry_add_object(void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version)
{
    if (strcmp(interface, zwlr_input_inhibit_manager_v1_interface.name) == 0)
    {
        inhibitor_manager = (zwlr_input_inhibit_manager_v1*)wl_registry_bind(
            registry, name, &zwlr_input_inhibit_manager_v1_interface, 1u);
    }
}

static void registry_remove_object(void *data, struct wl_registry *registry,
    uint32_t name)
{
    /* no-op */
}

static struct wl_registry_listener registry_listener =
{
    &registry_add_object,
    &registry_remove_object
};


int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cout << "Usage: wf-nk-server <port>" << std::endl;

        return 0;
    }

    server_port = std::atoi(argv[1]);
    std::cout << "Using port " << server_port << std::endl;

    auto app = Gtk::Application::create();
    app->signal_activate().connect([=] ()
    {
        app->hold();

        /* Load input inhibit protocol */
        auto disp = gdk_wayland_display_get_wl_display(gdk_display_get_default());
        assert(disp);
        auto registry = wl_display_get_registry(disp);
        wl_registry_add_listener(registry, &registry_listener, &disp);
        wl_display_dispatch(disp);
        wl_display_roundtrip(disp);

        if (!inhibitor_manager)
        {
            std::cerr << "Compositor does not support " <<
                " wlr_input_inhibit_manager_v1!" << std::endl;
            std::exit(-1);
        }

        screen_lock =
            zwlr_input_inhibit_manager_v1_get_inhibitor(inhibitor_manager);

        auto win = new ServerWindow();
        win->show_all();
    });

    KeyServer::get(); // wait for client

    return app->run();
}
