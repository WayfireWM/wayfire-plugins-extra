/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2025 Scott Moreau <oreaus@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <wayfire/view.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/plugins/animate/animate.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>

#include "blinds.hpp"
#include "helix.hpp"
#include "shatter.hpp"
#include "vortex.hpp"
#include "melt.hpp"
#include "dodge.hpp"
#include "burn.hpp"

class wayfire_extra_animations : public wf::plugin_interface_t
{
    wf::shared_data::ref_ptr_t<wf::animate::animate_effects_registry_t> effects_registry;
    wf::option_wrapper_t<bool> dodge_toggle{"extra-animations/dodge_toggle"};
    wf::option_wrapper_t<wf::animation_description_t> blinds_duration{"extra-animations/blinds_duration"};
    wf::option_wrapper_t<wf::animation_description_t> helix_duration{"extra-animations/helix_duration"};
    wf::option_wrapper_t<wf::animation_description_t> shatter_duration{"extra-animations/shatter_duration"};
    wf::option_wrapper_t<wf::animation_description_t> vortex_duration{"extra-animations/vortex_duration"};
    wf::option_wrapper_t<wf::animation_description_t> melt_duration{"extra-animations/melt_duration"};
    wf::option_wrapper_t<wf::animation_description_t> burn_duration{"extra-animations/burn_duration"};
    std::unique_ptr<wf::dodge::wayfire_dodge> dodge_plugin;

  public:
    void init() override
    {
        if (!wf::get_core().is_gles2())
        {
            LOGE("wayfire-extra-animations: not supported on non-gles2 wayfire");
            return;
        }

        effects_registry->register_effect("blinds", wf::animate::effect_description_t{
            .generator = [] { return std::make_unique<wf::blinds::blinds_animation>(); },
            .default_duration = [=] { return blinds_duration.value(); },
        });
        effects_registry->register_effect("helix", wf::animate::effect_description_t{
            .generator = [] { return std::make_unique<wf::helix::helix_animation>(); },
            .default_duration = [=] { return helix_duration.value(); },
        });
        effects_registry->register_effect("shatter", wf::animate::effect_description_t{
            .generator = [] { return std::make_unique<wf::shatter::shatter_animation>(); },
            .default_duration = [=] { return shatter_duration.value(); },
        });
        effects_registry->register_effect("vortex", wf::animate::effect_description_t{
            .generator = [] { return std::make_unique<wf::vortex::vortex_animation>(); },
            .default_duration = [=] { return vortex_duration.value(); },
        });
        effects_registry->register_effect("melt", wf::animate::effect_description_t{
            .generator = [] { return std::make_unique<wf::melt::melt_animation>(); },
            .default_duration = [=] { return melt_duration.value(); },
        });
        effects_registry->register_effect("burn", wf::animate::effect_description_t{
            .generator = [] { return std::make_unique<wf::burn::burn_animation>(); },
            .default_duration = [=] { return burn_duration.value(); },
        });
        dodge_toggle.set_callback([=] {dodge_option_changed();});
        dodge_option_changed();
    }

    void dodge_option_changed()
    {
        if (dodge_toggle && !dodge_plugin)
        {
            dodge_plugin = std::make_unique<wf::dodge::wayfire_dodge>();
            dodge_plugin->init();
        } else if (!dodge_toggle && dodge_plugin)
        {
            dodge_plugin->fini();
            dodge_plugin.reset();
        }
    }

    void fini() override
    {
        effects_registry->unregister_effect("blinds");
        effects_registry->unregister_effect("helix");
        effects_registry->unregister_effect("shatter");
        effects_registry->unregister_effect("vortex");
        effects_registry->unregister_effect("melt");
        effects_registry->unregister_effect("burn");

        if (dodge_plugin)
        {
            dodge_plugin->fini();
            dodge_plugin.reset();
        }
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_extra_animations);
