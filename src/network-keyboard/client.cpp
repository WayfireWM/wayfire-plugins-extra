#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/array.hpp>
#include <iostream>
#include <sstream>
#include <set>

#include <xkbcommon/xkbcommon.h>
#include <memory>
#include <sys/mman.h>
#include <wayland-client.h>

#include <virtual-keyboard-unstable-v1-client-protocol.h>
#include "shared/os-compatibility.h"

#include <time.h>

struct WaylandDisplay
{
    wl_display *display = nullptr;
    zwp_virtual_keyboard_manager_v1 *vk_manager = nullptr;
    wl_seat *seat = nullptr;
};

struct Modifiers
{
    uint32_t depressed;
    uint32_t latched;
    uint32_t locked;
    uint32_t group;

    bool operator ==(const Modifiers& other)
    {
        return depressed == other.depressed && latched == other.latched &&
               locked == other.locked && group == other.group;
    }

    bool operator !=(const Modifiers& other)
    {
        return !(*this == other);
    }
};

class VirtualKeyboardDevice
{
    zwp_virtual_keyboard_v1 *vk;
    void send_keymap()
    {
        /* The keymap string is defined in keymap.tpp, it is keymap_normal */
#include "keymap.tpp"

        size_t keymap_size = strlen(keymap) + 1;
        int keymap_fd = os_create_anonymous_file(keymap_size);
        void *ptr     = mmap(NULL, keymap_size, PROT_READ | PROT_WRITE, MAP_SHARED,
            keymap_fd, 0);

        std::strcpy((char*)ptr, keymap);
        zwp_virtual_keyboard_v1_keymap(vk, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
            keymap_fd, keymap_size);
    }

  public:
    VirtualKeyboardDevice(WaylandDisplay *display)
    {
        vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
            display->vk_manager, display->seat);

        this->send_keymap();
    }

    void send_key(uint32_t time, uint32_t key, uint32_t state) const
    {
        zwp_virtual_keyboard_v1_key(vk, time, key, state);
    }

    void send_modifiers(Modifiers mod)
    {
        zwp_virtual_keyboard_v1_modifiers(vk, mod.depressed,
            mod.latched, mod.locked, mod.group);
    }
};

// listeners
static void registry_add_object(void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version)
{
    auto display = static_cast<WaylandDisplay*>(data);
    if (strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0)
    {
        display->vk_manager = (zwp_virtual_keyboard_manager_v1*)
            wl_registry_bind(registry, name,
            &zwp_virtual_keyboard_manager_v1_interface, 1u);
    }

    if (!display->seat && (strcmp(interface, wl_seat_interface.name) == 0))
    {
        display->seat = (wl_seat*)
            wl_registry_bind(registry, name, &wl_seat_interface, 1u);
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

class NetworkKeyboardClient
{
    WaylandDisplay disp = {nullptr};
    std::unique_ptr<VirtualKeyboardDevice> device;
    std::stringstream stream;

    /* Do not allow long press, double press or whatever */
    std::set<uint32_t> pressed_keys;
    Modifiers last_modifiers = {0, 0, 0, 0};
    uint32_t last_timestamp  = 0;

    void process_event(uint32_t time, uint32_t key, uint32_t state)
    {
        last_timestamp = time;
        device->send_key(time, key, state);

        /* Update modifiers */
        xkb_state_update_key(xkb.state, key + 8,
            state ? XKB_KEY_DOWN : XKB_KEY_UP);

        Modifiers mods;
        mods.depressed = xkb_state_serialize_mods(xkb.state,
            XKB_STATE_MODS_DEPRESSED);
        mods.latched = xkb_state_serialize_mods(xkb.state,
            XKB_STATE_MODS_LATCHED);
        mods.locked = xkb_state_serialize_mods(xkb.state,
            XKB_STATE_MODS_LOCKED);
        mods.group = xkb_state_serialize_layout(xkb.state,
            XKB_STATE_LAYOUT_EFFECTIVE);

        if (mods != last_modifiers)
        {
            last_modifiers = mods;
            device->send_modifiers(mods);
        }
    }

    /**
     * Read a combination of time, key and state from the stream,
     * and send it
     */
    void read_single_event()
    {
        uint32_t time, keycode, state;
        if (stream >> time >> keycode >> state)
        {
            bool current_state = pressed_keys.count(keycode) > 0;
            if (current_state != state)
            {
                std::cout << "Received " << time << " " << keycode <<
                    " " << state << std::endl;
                process_event(time, keycode, state);

                if (state)
                {
                    pressed_keys.insert(keycode);
                } else
                {
                    pressed_keys.erase(keycode);
                }
            }
        }
    }

    struct
    {
        xkb_context *ctx;
        xkb_keymap *keymap;
        xkb_state *state;
    } xkb;

    void setup_xkb_state()
    {
#include "keymap.tpp"
        xkb.ctx    = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        xkb.keymap = xkb_keymap_new_from_string(xkb.ctx, keymap,
            XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
        xkb.state = xkb_state_new(xkb.keymap);
    }

  public:

    NetworkKeyboardClient()
    {
        /* Init wayland connection & protocol */
        disp.display = wl_display_connect(NULL);
        assert(disp.display);
        auto registry = wl_display_get_registry(disp.display);

        wl_registry_add_listener(registry, &registry_listener, &disp);
        wl_display_dispatch(disp.display);
        wl_display_roundtrip(disp.display);

        if (!disp.vk_manager)
        {
            std::cout << "Compositor does not support" <<
                " virtual-keyboard-v1 protocol!" << std::endl;
            std::exit(-1);
        }

        device = std::make_unique<VirtualKeyboardDevice>(&disp);

        wl_display_flush(disp.display);
        wl_display_roundtrip(disp.display);

        setup_xkb_state();
    }

    void release_all()
    {
        device->send_modifiers({0, 0, 0, 0});
        for (auto& key : pressed_keys)
        {
            device->send_key(last_timestamp, key, 0);
        }

        wl_display_flush(disp.display);
    }

    /**
     * Handle input data from the sever
     */
    void process_input(std::string input)
    {
        for (auto& c : input)
        {
            if (c == '$')
            {
                read_single_event();
                continue;
            }

            /* Read other characters and wait for the next event end */
            stream.clear();
            stream << c;
        }
    }

    /**
     * Run the wayland event loop for some time
     */
    void spin_some()
    {
        wl_display_flush(disp.display);
        wl_display_roundtrip(disp.display);
    }
};


using boost::asio::ip::tcp;

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cout << "Usage: wf-nk-client <server ip> <port>" << std::endl;

        return -1;
    }

    unsigned short port = std::atoi(argv[2]);
    std::cout << "Using server " << argv[1] << ":" << port << std::endl;

    boost::asio::io_context ctx;
    tcp::resolver resolver(ctx);
    tcp::endpoint endpoint{boost::asio::ip::make_address_v4(argv[1]), port};

    tcp::socket socket(ctx);
    socket.connect(endpoint);

    NetworkKeyboardClient client;
    while (true)
    {
        boost::array<char, 128> buf;
        boost::system::error_code error;

        size_t len = socket.read_some(boost::asio::buffer(buf), error);
        client.process_input(std::string{buf.data(), len});

        if (error == boost::asio::error::eof)
        {
            break; // Connection closed cleanly by peer.
        } else if (error)
        {
            break; // Some other error
        }

        client.spin_some();
    }

    client.release_all();
    std::cout << "Server shut down, shutting down client" << std::endl;
}
