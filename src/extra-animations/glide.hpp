/*
 * Glide, like Compiz's Glide animation: the
 * window tilts back about its horizontal centre line, recedes and fades
 * while it closes or minimises -- and comes forward and upright the same
 * way when it opens. Uses Wayfire's own 3D view transformer (perspective,
 * no shader of its own).
 */
#pragma once

#include <wayfire/plugins/animate/animate.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/option-wrapper.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace wf
{
namespace glide
{
using namespace wf::animate;
using namespace wf::animation;

static const std::string glide_transformer_name = "animation-glide";
wf::option_wrapper_t<double> glide_angle{"extra-animations/glide_angle"};

class glide_animation : public animation_base_t
{
    wayfire_view view;
    simple_animation_t progression;

  public:
    void init(wayfire_view view, wf::animation_description_t dur, animation_type type) override
    {
        this->view = view;
        this->progression = simple_animation_t(wf::create_option<wf::animation_description_t>(dur));
        /* 1 = in place, 0 = tilted away and invisible */
        if (type & WF_ANIMATE_HIDING_ANIMATION)
        {
            progression.animate(1, 0);
        } else
        {
            progression.animate(0, 1);
        }

        auto tr = std::make_shared<wf::scene::view_3d_transformer_t>(view);
        view->get_transformed_node()->add_transformer(tr, wf::TRANSFORMER_HIGHLEVEL,
            glide_transformer_name);
        update(tr.get());
    }

    void update(wf::scene::view_3d_transformer_t *tr)
    {
        const float p     = std::clamp((double)progression, 0.0, 1.0);
        const float angle = glm::radians(std::clamp((double)glide_angle, -90.0, 90.0)) * (1.0f - p);
        const float s     = 0.7f + 0.3f * p;
        /* the transformer's y points up and z towards the viewer: a
         * rotation by +a about x would bring the top edge forward; -a tilts
         * the top edge away (checked headless: top row narrower) */
        tr->rotation = glm::rotate(glm::mat4(1.0), -angle, glm::vec3(1, 0, 0));
        tr->scaling  = glm::scale(glm::mat4(1.0), glm::vec3(s, s, 1));
        tr->color    = glm::vec4(1, 1, 1, p);
    }

    bool step() override
    {
        auto tr = view->get_transformed_node()->get_transformer<wf::scene::view_3d_transformer_t>(
            glide_transformer_name);
        if (tr)
        {
            update(tr.get()); /* the animate plugin repaints every frame */
        }

        return progression.running();
    }

    void reverse() override
    {
        progression.reverse();
    }

    ~glide_animation()
    {
        view->get_transformed_node()->rem_transformer(glide_transformer_name);
    }
};
}
}
