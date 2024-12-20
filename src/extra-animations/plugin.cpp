/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 Scott Moreau <oreaus@gmail.com>
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

class wayfire_extra_animations : public wf::plugin_interface_t
{
    wf::shared_data::ref_ptr_t<wf::animate::animate_effects_registry_t> effects_registry;

  public:
    void init() override
    {
        effects_registry->register_effect("blinds", wf::animate::effect_description_t{
            .generator = [] { return std::make_unique<wf::blinds::blinds_animation>(); },
            .default_duration = [=] { return wf::blinds::blinds_duration.value(); },
        });
        effects_registry->register_effect("helix", wf::animate::effect_description_t{
            .generator = [] { return std::make_unique<wf::helix::helix_animation>(); },
            .default_duration = [=] { return wf::helix::helix_duration.value(); },
        });
        effects_registry->register_effect("shatter", wf::animate::effect_description_t{
            .generator = [] { return std::make_unique<wf::shatter::shatter_animation>(); },
            .default_duration = [=] { return wf::shatter::shatter_duration.value(); },
        });
        effects_registry->register_effect("vortex", wf::animate::effect_description_t{
            .generator = [] { return std::make_unique<wf::vortex::vortex_animation>(); },
            .default_duration = [=] { return wf::vortex::vortex_duration.value(); },
        });
    }

    void fini() override
    {
        effects_registry->unregister_effect("blinds");
        effects_registry->unregister_effect("helix");
        effects_registry->unregister_effect("shatter");
        effects_registry->unregister_effect("vortex");
    }
};

DECLARE_WAYFIRE_PLUGIN(wayfire_extra_animations);
