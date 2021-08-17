#include "wayfire/plugin.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/input-device.hpp"
#include <wayfire/util/log.hpp>
#include <fstream>
#include <vector>
#include <string>

class wayfire_hinge : public wf::plugin_interface_t
{
    wf::option_wrapper_t<std::string> file_name{"hinge/filename"};
    wf::option_wrapper_t<int> poll_freq{"hinge/poll_freq"};
    wf::option_wrapper_t<int> flip_degree{"hinge/flip_degree"};
    
    std::ifstream device_file;
    wf::wl_timer* timer;

    bool input_enabled = true;

    std::vector<nonstd::observer_ptr<wf::input_device_t>> get_inputs() {
        wf::compositor_core_t* core = &wf::get_core();
        return core->get_input_devices();
    }

    bool read_device() 
    {   
        char buf[4];
        device_file.seekg(0);
        device_file.readsome(buf, 4);

        if(device_file.fail()) {
            LOGE("Failed reading from hinge sensor device: ", device_file.rdstate());
            return false;
        }
        
        int angle = std::stoi(buf);
        if(angle < 0 || angle > 360) {
            LOGE("Read invalid data from hinge sensor: ", angle);
            enable_inputs(); // So we don't get stuck with disabled inputs
            return true;
        }

        bool new_state = angle < flip_degree;
        if(new_state != input_enabled) {
            if(new_state) enable_inputs();
            else disable_inputs();
        }

        return true;
    }



    void disable_inputs() {
        input_enabled = false;
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
        input_enabled = true;
        for (auto inp: get_inputs())
        {
            inp->set_enabled();
        }
    }

    public:

        void init() override
        {
            device_file.open(file_name, std::ifstream::in);

            timer = new wf::wl_timer();
            timer->set_timeout(poll_freq, [this] ()
            {
                return read_device();
            });
        }

        void fini() override
        { 
            enable_inputs();
            device_file.close();
        }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_hinge);
