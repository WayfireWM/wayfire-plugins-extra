#include "wayfire/plugin.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/input-device.hpp"
#include <wayfire/util/log.hpp>
#include <fstream>
#include <vector>
#include <string>
#include <unistd.h>
#include <thread>

class wayfire_hinge : public wf::plugin_interface_t
{
    enum thread_message: char {
        ENABLE_INPUT,
        DISABLE_INPUT,
        THREAD_EXIT
    };

    wf::option_wrapper_t<std::string> file_name{"hinge/filename"};
    wf::option_wrapper_t<int> poll_freq{"hinge/poll_freq"};
    wf::option_wrapper_t<int> flip_degree{"hinge/flip_degree"};
    
    int pipefd[2];
    std::thread thread;
    wl_event_source* pipe_reader;
    bool exiting = false;

    std::vector<nonstd::observer_ptr<wf::input_device_t>> get_inputs() {
        wf::compositor_core_t* core = &wf::get_core();
        return core->get_input_devices();
    }

    void disable_inputs() {
        for (auto inp: get_inputs())
        {
            auto inp_type = inp->get_wlr_handle()->type;
            if(
            inp_type == wlr_input_device_type::WLR_INPUT_DEVICE_KEYBOARD || 
            inp_type == wlr_input_device_type::WLR_INPUT_DEVICE_POINTER
            ) {
                inp->set_enabled(false);
            }
        }
    }

    void enable_inputs() {
        for (auto inp: get_inputs())
        {
            inp->set_enabled();
        }
    }

    static void setup_thread(std::string fn, int poll_freq, int flip_degree, bool* exiting, int pipe) {
        std::ifstream device_file(fn, std::ifstream::in);
        bool input_enabled = true;

        while(!*exiting) {
            char buf[4];
            device_file.seekg(0);
            device_file.readsome(buf, 4);

            if(device_file.fail()) {
                LOGE("Failed reading from hinge sensor device: ", device_file.rdstate());
                send_message(thread_message::THREAD_EXIT, pipe);
                device_file.close();
                break;
            }
            
            int angle = std::stoi(buf);
            if(angle < 0 || angle > 360) {
                LOGE("Read invalid data from hinge sensor: ", angle);
                send_message(thread_message::THREAD_EXIT, pipe);
                device_file.close();
                break;
            }

            bool new_state = angle < flip_degree;
            if(new_state != input_enabled) {
                if(new_state) send_message(thread_message::ENABLE_INPUT, pipe);
                else send_message(thread_message::DISABLE_INPUT, pipe);
                input_enabled = new_state;
            }
            usleep(poll_freq * 1000); // microseconds*1000=ms
        }
        close(pipe);
        device_file.close();
    }

    static void send_message(thread_message message, int pipe) {
        write(pipe, &message, 1);
    }

    static int on_pipe_update(int fd, uint32_t mask, void *data) {
        wayfire_hinge* that = (wayfire_hinge*) data;
        thread_message message;
        read(that->pipefd[0], &message, 1);
        switch (message) {
            case thread_message::ENABLE_INPUT: 
                that->enable_inputs();
                break;
            case thread_message::DISABLE_INPUT: 
                that->disable_inputs();
                break;
            case thread_message::THREAD_EXIT: 
                that->enable_inputs(); // So we don't get stuck in an unusable state
                return 0;
        }
        return 1;
    }

    public:

        void init() override
        {
            if (pipe(pipefd) == -1) {
               LOGE("Failed to open pipe");
               return;
            }
            pipe_reader = wl_event_loop_add_fd(wl_display_get_event_loop(wf::get_core().display), pipefd[0], WL_EVENT_READABLE, on_pipe_update, this);
            thread = std::thread(setup_thread, file_name.value(), poll_freq.value(), flip_degree.value(), &exiting, pipefd[1]);
        }

        void fini() override
        { 
            enable_inputs();
            wl_event_source_remove(pipe_reader);

            exiting = true;
            thread.join();

            close(pipefd[0]);
        }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_hinge);
